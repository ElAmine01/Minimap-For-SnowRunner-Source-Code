#pragma once
/// ImGui lifecycle. Kept outside MinimapRenderer so the latter only deals
/// with its own geometry and not with global state.

#include <windows.h>   // HWND

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace snowmap::render {

/// ImGui lifecycle helper bound to the game swap chain.
class ImGuiManager
{
public:
    /// Initialize ImGui for the game window.
    static bool Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, HWND hwnd);
    /// Shutdown ImGui and restore window state.
    static void Shutdown();

    /// Start a new ImGui frame (poll input, begin draw).
    static void BeginFrame();
    /// Render current ImGui draw data to the back buffer.
    static void EndFrame();

    /// Call before swap-chain resize to drop RTVs and rebuild after.
    static void OnResize();
};

} // namespace snowmap::render
