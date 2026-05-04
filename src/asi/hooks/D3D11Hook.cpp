#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "D3D11Hook.h"
#include "../core/Logger.h"
#include "../core/Globals.h"
#include <MinHook.h>
#include <d3d11.h>
#include <mutex>

namespace snowmap::hooks {
namespace {

using CreateTexture2D_Fn = HRESULT (WINAPI*)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
CreateTexture2D_Fn g_origCreateTexture2D = nullptr;

std::mutex                g_capMu;
ID3D11ShaderResourceView* g_capturedSRV = nullptr;
uint32_t                  g_capW = 0;
uint32_t                  g_capH = 0;

HRESULT WINAPI hkCreateTexture2D(ID3D11Device* pDevice, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D) {
    HRESULT hr = g_origCreateTexture2D(pDevice, pDesc, pInitialData, ppTexture2D);
    
    if (SUCCEEDED(hr) && pDesc && ppTexture2D && *ppTexture2D) {
        // Capture large square UI textures (maps/splatmaps) for minimap use.
        if (pDesc->Width >= 1024 && pDesc->Width <= 4096 && pDesc->Height == pDesc->Width && pDesc->ArraySize == 1) {
            if (pDesc->BindFlags & D3D11_BIND_SHADER_RESOURCE) {
                ID3D11ShaderResourceView* srv = nullptr;
                if (SUCCEEDED(pDevice->CreateShaderResourceView(*ppTexture2D, nullptr, &srv))) {
                    std::lock_guard<std::mutex> lk(g_capMu);
                    if (g_capturedSRV) g_capturedSRV->Release();
                    g_capturedSRV = srv;
                    g_capW = pDesc->Width;
                    g_capH = pDesc->Height;
                }
            }
        }
    }
    return hr;
}

} // namespace

bool InstallD3D11Hook() {
    if (!G().device) return false;

    void** devVtable = *reinterpret_cast<void***>(G().device);
    void* createTex2DTarget = devVtable[5];

    if (MH_CreateHook(createTex2DTarget, &hkCreateTexture2D, reinterpret_cast<void**>(&g_origCreateTexture2D)) != MH_OK)
        return false;
    MH_EnableHook(createTex2DTarget);
    SM_INFO("D3D11Hook installed (CreateTexture2D).");
    return true;
}

void RemoveD3D11Hook() {
    std::lock_guard<std::mutex> lk(g_capMu);
    if (g_capturedSRV) { g_capturedSRV->Release(); g_capturedSRV = nullptr; }
}

ID3D11ShaderResourceView* AcquireMinimapSRV() {
    std::lock_guard<std::mutex> lk(g_capMu);
    if (g_capturedSRV) {
        g_capturedSRV->AddRef();
        return g_capturedSRV;
    }
    return nullptr;
}

void GetMinimapSRVSize(uint32_t& w, uint32_t& h) {
    std::lock_guard<std::mutex> lk(g_capMu);
    w = g_capW;
    h = g_capH;
}

} // namespace snowmap::hooks