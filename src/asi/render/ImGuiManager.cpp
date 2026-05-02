#include "ImGuiManager.h"
#include "../core/Logger.h"
#include "../core/Globals.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

// The Win32 backend exposes its own WndProc helper we chain into.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND, UINT, WPARAM, LPARAM);

namespace snowmap::render {
namespace {

bool             g_initialized = false;
WNDPROC          g_origWndProc = nullptr;
HWND             g_hwnd        = nullptr;
ID3D11RenderTargetView* g_mainRTV = nullptr;

// ---- WndProc -------------------------------------------------------------
//
// We subclass the game's window so ImGui can see input. When the overlay is
// visible AND ImGui wants the event (hovering a window, typing in a text box)
// we eat the message so it doesn't reach the game — otherwise we pass it on.
LRESULT CALLBACK WndProcHook(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) {
        ImGuiIO& io = ImGui::GetIO();
        const bool mouseOwned = io.WantCaptureMouse &&
            (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
             msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP || msg == WM_MBUTTONDOWN ||
             msg == WM_MBUTTONUP || msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL);
        const bool kbOwned = io.WantCaptureKeyboard &&
            (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR ||
             msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP);
        if (mouseOwned || kbOwned) return TRUE;
    }
    return CallWindowProcW(g_origWndProc, h, msg, w, l);
}

// ---- RTV helpers ---------------------------------------------------------
bool CreateMainRTV()
{
    if (!snowmap::G().swapChain || !snowmap::G().device) return false;
    ID3D11Texture2D* back = nullptr;
    if (FAILED(snowmap::G().swapChain->GetBuffer(
            0, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&back)))) return false;

    HRESULT hr = snowmap::G().device->CreateRenderTargetView(back, nullptr, &g_mainRTV);
    back->Release();
    return SUCCEEDED(hr);
}

void ReleaseMainRTV()
{
    if (g_mainRTV) { g_mainRTV->Release(); g_mainRTV = nullptr; }
}

} // namespace

bool ImGuiManager::Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, HWND hwnd)
{
    if (g_initialized) return true;
    if (!dev || !ctx || !hwnd) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // game owns the cursor
    io.IniFilename  = nullptr;                              // no imgui.ini clutter

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(hwnd)) {
        SM_ERROR("ImGui_ImplWin32_Init failed.");
        return false;
    }
    if (!ImGui_ImplDX11_Init(dev, ctx)) {
        SM_ERROR("ImGui_ImplDX11_Init failed.");
        return false;
    }

    // Subclass the game window so we capture input.
    g_hwnd = hwnd;
    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(&WndProcHook)));

    if (!CreateMainRTV()) {
        SM_ERROR("CreateMainRTV failed.");
        return false;
    }

    g_initialized = true;
    SM_INFO("ImGui initialised.");
    return true;
}

void ImGuiManager::Shutdown()
{
    if (!g_initialized) return;
    ReleaseMainRTV();
    if (g_hwnd && g_origWndProc) {
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(g_origWndProc));
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_initialized = false;
}

void ImGuiManager::BeginFrame()
{
    if (!g_initialized) return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::EndFrame()
{
    if (!g_initialized) return;
    ImGui::Render();
    // RTV may be null right after a resize — recreate on demand.
    if (!g_mainRTV) CreateMainRTV();
    if (g_mainRTV) {
        // Bind the back buffer without touching depth — we're pure overlay.
        snowmap::G().context->OMSetRenderTargets(1, &g_mainRTV, nullptr);
    }
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiManager::OnResize()
{
    ReleaseMainRTV();
    // CreateMainRTV is called again on the next BeginFrame, once the
    // swap chain has finished resizing.
}

} // namespace snowmap::render
