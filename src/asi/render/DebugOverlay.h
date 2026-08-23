#pragma once
/// In-game diagnostics and live settings editor, toggled with debug_key (F7).
///
/// Everything it shows is state the mod already tracks; the point is to make a
/// misbehaving install diagnosable without reading the log, and to let the user
/// retune the minimap without alt-tabbing to options.json.

#include <string>

struct ID3D11ShaderResourceView;

namespace snowmap::render {

/// Renderer state for the current frame, filled by MinimapRenderer.
struct DebugInfo
{
    std::string level_id;

    bool satellite_ready = false;
    int  satellite_w     = 0;
    int  satellite_h     = 0;
    bool captured_srv    = false;

    bool download_active = false;
    bool download_failed = false;
    bool download_done   = false;

    ID3D11ShaderResourceView* blips_srv = nullptr;
    int  blips_w      = 0;
    int  blips_h      = 0;

    bool  player_valid   = false;
    float player_world[3] = {0.f, 0.f, 0.f};
    float player_heading  = 0.f;
    float uv_center[2]    = {0.f, 0.f};

    int drawn_blips = 0;
};

class DebugOverlay
{
public:
    static bool Visible();
    static void SetVisible(bool visible);
    static void Toggle();

    /// Draw the window. No-op while hidden.
    static void Draw(const DebugInfo& info);
};

} // namespace snowmap::render
