#pragma once
/// Runtime-tunable options, loaded from <gamedir>/SnowMap/options.json.
/// Kept intentionally flat; it is a simple POD-style struct.
///
/// We reload on demand (F6 by default) so users can iterate without
/// restarting the game.

#include <cstdint>
#include <string>

namespace snowmap {

/// User-facing configuration for the minimap and input bindings.
struct Config
{
    // Minimap on-screen placement (pixels from chosen anchor).
    int  position_x      = 20;
    int  position_y      = 20;
    int  size_px         = 320;
    enum Anchor : int { TopLeft = 0, TopRight = 1, BottomLeft = 2, BottomRight = 3 };
    Anchor anchor        = BottomLeft;

    // Visual behaviour.
    float zoom           = 1.0f;   // 1.0 = fit, >1 zooms in
    float opacity        = 0.85f;
    bool  rotate_with_player = false;
    bool  show_player_arrow  = true;

    // Input — virtual-key codes (VK_F5 etc.). Documented in options.json.
    int  toggle_key      = 0x74;   // F5
    int  reload_key      = 0x75;   // F6
    int  zoom_in_key     = 0xBB;   // VK_OEM_PLUS
    int  zoom_out_key    = 0xBD;   // VK_OEM_MINUS

    // Input — XInput buttons (XINPUT_GAMEPAD_*). 0 disables.
    uint16_t pad_toggle_btn   = 0;
    uint16_t pad_reload_btn   = 0;
    uint16_t pad_zoom_in_btn  = 0;
    uint16_t pad_zoom_out_btn = 0;

    // Asset overrides.
    std::string blips_png_path   = "SnowMap/blips.png";

    // Capture filter: reject D3D11 texture candidates larger than this (pixels).
    // Default 5000 admits 4036 satellite textures but blocks absurdly large
    // streaming tiles.  Set 0 to disable.
    int   max_capture_size_px = 5000;

    // Debug.
    bool  draw_debug_window = false;

    /// Load/reload from disk. Returns false if the file is unreadable.
    bool LoadFromFile(const char* path);

    /// Resolve <gamedir>/SnowMap/options.json into `out` (MAX_PATH buffer).
    static bool ResolveDefaultPath(char* out, size_t cap);
};

/// Global singleton; one config per process is enough.
Config& GetConfig();

} // namespace snowmap
