#pragma once
//  ImGui lifecycle. Kept outside MinimapRenderer so the latter only deals
//  with its own geometry and not with global state.

#include <windows.h>   // HWND

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace snowmap::render {

class ImGuiManager
{
public:
    static bool Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, HWND hwnd);
    static void Shutdown();

    static void BeginFrame();   // NewFrame, poll input, etc.
    static void EndFrame();     // Render to back buffer, present-safe

    // Call when the swap chain is about to be resized — we must tear down
    // the render-target view and rebuild it post-resize.
    static void OnResize();
};

} // namespace snowmap::render
