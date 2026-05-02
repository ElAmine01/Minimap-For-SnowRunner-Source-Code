#pragma once
//  Shared, mutable state across the mod.
//
//  This file exists because the D3D11 hook, the input loop, and the
//  renderer all need to agree on the same device/context/SwapChain. Rather
//  than threading those handles through every call, we park them in one
//  place with an explicit lifetime.
//
//  Anything in here that is written by hooks is protected by a spinlock
//  (see Globals.cpp). The intent is *not* to share mutable game state —
//  for that we use PlayerState, which has its own synchronization.

#include <atomic>
#include <windows.h>   // HWND — must come before D3D forward-decls
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

namespace snowmap {

struct Globals
{
    // --- D3D11 objects (captured the first time Present is invoked) ---
    ID3D11Device*        device       = nullptr;
    ID3D11DeviceContext* context      = nullptr;
    IDXGISwapChain*      swapChain    = nullptr;
    HWND                 gameWindow   = nullptr;

    // --- Lifecycle flags ---
    std::atomic<bool>    imguiReady{false};
    std::atomic<bool>    minimapTextureLoaded{false};
    std::atomic<bool>    requestReloadConfig{false};
    std::atomic<bool>    overlayVisible{true};
};

Globals& G();

} // namespace snowmap
