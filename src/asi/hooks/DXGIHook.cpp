#include "DXGIHook.h"
#include "HookManager.h"
#include "../core/Logger.h"
#include "../core/Globals.h"
#include "../render/ImGuiManager.h"
#include "../render/MinimapRenderer.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>

namespace snowmap::hooks {

namespace {

// ------------------------------------------------------------------ typedefs
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn  = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

PresentFn g_origPresent = nullptr;
ResizeFn  g_origResize  = nullptr;

// --------------------------------------------------------------- detours
HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain* swap, UINT sync, UINT flags)
{
    // Lazy device/context capture. We only need to do it once.
    if (!G().device) {
        if (SUCCEEDED(swap->GetDevice(__uuidof(ID3D11Device),
                                      reinterpret_cast<void**>(&G().device))))
        {
            G().device->GetImmediateContext(&G().context);

            DXGI_SWAP_CHAIN_DESC desc{};
            swap->GetDesc(&desc);
            G().gameWindow = desc.OutputWindow;
            G().swapChain  = swap;

            SM_INFO("Captured D3D11 device=%p ctx=%p hwnd=%p",
                    static_cast<void*>(G().device),
                    static_cast<void*>(G().context),
                    static_cast<void*>(G().gameWindow));
        }
    }

    // Initialise ImGui once the device is known. ImGuiManager is responsible
    // for being idempotent and for handling lost-device scenarios.
    if (G().device && !G().imguiReady.load(std::memory_order_acquire)) {
        if (render::ImGuiManager::Init(G().device, G().context, G().gameWindow)) {
            G().imguiReady.store(true, std::memory_order_release);
            // Now that we have a context, install CB-sniffing hooks.
            InstallDeviceHooks();
        }
    }

    // Reload config on request (triggered by F6 in the input loop).
    if (G().requestReloadConfig.exchange(false, std::memory_order_acq_rel)) {
        render::MinimapRenderer::OnConfigReloaded();
    }

    if (G().imguiReady.load(std::memory_order_acquire)) {
        render::ImGuiManager::BeginFrame();
        render::MinimapRenderer::Draw();
        render::ImGuiManager::EndFrame();
    }

    return g_origPresent(swap, sync, flags);
}

HRESULT STDMETHODCALLTYPE hkResizeBuffers(IDXGISwapChain* swap,
                                          UINT count, UINT w, UINT h,
                                          DXGI_FORMAT fmt, UINT flags)
{
    // RTVs bound to the old back buffer must be dropped *before* resize,
    // or DXGI will return DXGI_ERROR_INVALID_CALL.
    if (G().imguiReady.load(std::memory_order_acquire)) {
        render::ImGuiManager::OnResize();
    }
    return g_origResize(swap, count, w, h, fmt, flags);
}

// --------------------------------------------------------- vtable discovery
//
// Create a hidden 1x1 window + a throw-away swap chain, read the vtable, and
// hand the method pointers to MinHook. The probe objects are released before
// we return.
bool ProbeVTable(void** outPresent, void** outResize)
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SnowMapProbe";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                0, 0, 1, 1, nullptr, nullptr,
                                wc.hInstance, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount       = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow      = hwnd;
    desc.SampleDesc.Count  = 1;
    desc.Windowed          = TRUE;
    desc.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device*        dev  = nullptr;
    ID3D11DeviceContext* ctx  = nullptr;
    IDXGISwapChain*      swap = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        &fl, 1, D3D11_SDK_VERSION, &desc, &swap, &dev, nullptr, &ctx);

    if (FAILED(hr)) {
        // DXVK/WARP fallback — in case hardware creation fails during startup
        // (DXVK initialises asynchronously on first load).
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            &fl, 1, D3D11_SDK_VERSION, &desc, &swap, &dev, nullptr, &ctx);
    }

    bool ok = false;
    if (SUCCEEDED(hr) && swap) {
        void** vtable = *reinterpret_cast<void***>(swap);
        *outPresent = vtable[8];   // IDXGISwapChain::Present
        *outResize  = vtable[13];  // IDXGISwapChain::ResizeBuffers
        ok = true;
    } else {
        SM_ERROR("Probe swap chain creation failed: 0x%08lX", hr);
    }

    if (swap) swap->Release();
    if (ctx)  ctx->Release();
    if (dev)  dev->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

} // namespace

// ---------------------------------------------------------------- public API
bool InstallDXGIHook()
{
    void* pPresent = nullptr;
    void* pResize  = nullptr;
    if (!ProbeVTable(&pPresent, &pResize)) return false;

    if (MH_CreateHook(pPresent, &hkPresent,
                      reinterpret_cast<void**>(&g_origPresent)) != MH_OK) {
        SM_ERROR("MH_CreateHook(Present) failed");
        return false;
    }
    if (MH_CreateHook(pResize, &hkResizeBuffers,
                      reinterpret_cast<void**>(&g_origResize)) != MH_OK) {
        SM_ERROR("MH_CreateHook(ResizeBuffers) failed");
        return false;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        SM_ERROR("MH_EnableHook failed");
        return false;
    }

    SM_INFO("DXGI hooks installed (Present=%p, Resize=%p)", pPresent, pResize);
    return true;
}

void RemoveDXGIHook()
{
    MH_DisableHook(MH_ALL_HOOKS);
}

} // namespace snowmap::hooks
