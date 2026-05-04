#pragma once
#include <string>

struct ID3D11ShaderResourceView;

namespace snowmap::render {

/// Main minimap rendering entry point and RAM-driven state updates.
class MinimapRenderer
{
public:
    /// Render one minimap frame (no-op if overlay is hidden).
    static void Draw();
    /// Refresh caches and textures after options.json reload.
    static void OnConfigReloaded();
    /// Release textures and shutdown background work.
    static void Shutdown();
    /// Return the currently active map SRV (may be null).
    static ID3D11ShaderResourceView* GetActiveMapSRV();

    /// Notify the renderer that a level ID was detected (thread-safe).
    static void OnLevelDetected(const std::string& levelId);
};

} // namespace snowmap::render
