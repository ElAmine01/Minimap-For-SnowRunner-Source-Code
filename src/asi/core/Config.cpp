#include "Config.h"
#include "Logger.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace snowmap {

using nlohmann::json;

namespace {

// Utility: json.value() with a log line when a field is missing — lets us
// spot typos in options.json without crashing the mod.
template <typename T>
T GetField(const json& j, const char* key, T fallback)
{
    auto it = j.find(key);
    if (it == j.end()) return fallback;
    try { return it->get<T>(); }
    catch (const std::exception& e) {
        SM_WARN("options.json: field '%s' has wrong type (%s), using default.",
                key, e.what());
        return fallback;
    }
}

} // namespace

bool Config::ResolveDefaultPath(char* out, size_t cap)
{
    char exePath[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exePath, MAX_PATH)) return false;
    PathRemoveFileSpecA(exePath);
    return _snprintf_s(out, cap, _TRUNCATE,
                       "%s\\SnowMap\\options.json", exePath) > 0;
}

bool Config::LoadFromFile(const char* path)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        SM_WARN("Config: cannot open '%s' — keeping defaults.", path);
        return false;
    }

    json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        SM_ERROR("Config: JSON parse error in '%s': %s", path, e.what());
        return false;
    }

    position_x          = GetField(j, "position_x",          position_x);
    position_y          = GetField(j, "position_y",          position_y);
    size_px             = GetField(j, "size_px",             size_px);
    anchor              = static_cast<Anchor>(GetField<int>(j, "anchor", anchor));
    zoom                = GetField(j, "zoom",                zoom);
    opacity             = GetField(j, "opacity",             opacity);
    rotate_with_player  = GetField(j, "rotate_with_player",  rotate_with_player);
    show_player_arrow   = GetField(j, "show_player_arrow",   show_player_arrow);
    toggle_key          = GetField(j, "toggle_key",          toggle_key);
    reload_key          = GetField(j, "reload_key",          reload_key);
    debug_key           = GetField(j, "debug_key",           debug_key);
    waypoint_key        = GetField(j, "waypoint_key",        waypoint_key);
    zoom_in_key         = GetField(j, "zoom_in_key",         zoom_in_key);
    zoom_out_key        = GetField(j, "zoom_out_key",        zoom_out_key);
    pad_toggle_btn      = static_cast<uint16_t>(GetField<int>(j, "pad_toggle_btn",   static_cast<int>(pad_toggle_btn)));
    pad_reload_btn      = static_cast<uint16_t>(GetField<int>(j, "pad_reload_btn",   static_cast<int>(pad_reload_btn)));
    pad_zoom_in_btn     = static_cast<uint16_t>(GetField<int>(j, "pad_zoom_in_btn",  static_cast<int>(pad_zoom_in_btn)));
    pad_zoom_out_btn    = static_cast<uint16_t>(GetField<int>(j, "pad_zoom_out_btn", static_cast<int>(pad_zoom_out_btn)));
    pad_waypoint_btn    = static_cast<uint16_t>(GetField<int>(j, "pad_waypoint_btn", static_cast<int>(pad_waypoint_btn)));
    blips_png_path      = GetField<std::string>(j, "blips_png_path",   blips_png_path);

    show_blips          = GetField(j, "show_blips",           show_blips);
    blip_size_px        = GetField(j, "blip_size_px",         blip_size_px);
    player_marker_px    = GetField(j, "player_marker_px",     player_marker_px);
    blip_atlas_cell_px  = GetField(j, "blip_atlas_cell_px",   blip_atlas_cell_px);
    blip_atlas_columns  = GetField(j, "blip_atlas_columns",   blip_atlas_columns);
    blip_rotate_icons   = GetField(j, "blip_rotate_icons",    blip_rotate_icons);
    blip_max_distance   = GetField(j, "blip_max_distance",    blip_max_distance);

    blip_icon_player    = GetField(j, "blip_icon_player",     blip_icon_player);
    blip_icon_waypoint  = GetField(j, "blip_icon_waypoint",   blip_icon_waypoint);
    blip_show_waypoints = GetField(j, "blip_show_waypoints",  blip_show_waypoints);

    max_capture_size_px = GetField(j, "max_capture_size_px", max_capture_size_px);
    draw_debug_window   = GetField(j, "draw_debug_window",   draw_debug_window);

    // Clamp here rather than at the key handler so a hand-edited options.json
    // cannot start the mod outside the stepped zoom range.
    zoom = std::clamp(zoom, kZoomMin, kZoomMax);

    SM_INFO("Config loaded from '%s'.", path);
    return true;
}

bool Config::SaveToFile(const char* path) const
{
    nlohmann::ordered_json j;

    j["_comment"]     = "SnowMap options. Reload key rereads this file; the debug overlay writes it.";
    j["_anchor_help"] = "0 = TopLeft, 1 = TopRight, 2 = BottomLeft, 3 = BottomRight";
    j["anchor"]       = static_cast<int>(anchor);
    j["position_x"]   = position_x;
    j["position_y"]   = position_y;
    j["size_px"]      = size_px;

    j["zoom"]               = zoom;
    j["opacity"]            = opacity;
    j["rotate_with_player"] = rotate_with_player;
    j["show_player_arrow"]  = show_player_arrow;

    j["_key_help"]    = "Windows virtual-key codes in decimal. F5=116 F6=117 F7=118 F8=119 Plus=187 Minus=189";
    j["toggle_key"]   = toggle_key;
    j["reload_key"]   = reload_key;
    j["debug_key"]    = debug_key;
    j["waypoint_key"] = waypoint_key;
    j["zoom_in_key"]  = zoom_in_key;
    j["zoom_out_key"] = zoom_out_key;

    j["_pad_help"]         = "XINPUT_GAMEPAD_* bitmask, 0 disables. A=4096 B=8192 X=16384 Y=32768 LB=256 RB=512";
    j["pad_toggle_btn"]    = pad_toggle_btn;
    j["pad_reload_btn"]    = pad_reload_btn;
    j["pad_zoom_in_btn"]   = pad_zoom_in_btn;
    j["pad_zoom_out_btn"]  = pad_zoom_out_btn;
    j["pad_waypoint_btn"]  = pad_waypoint_btn;

    j["blips_png_path"]     = blips_png_path;
    j["_blip_help"]         = "blips.png is a grid of square cells, left to right. Shipped sheet: "
                              "0 arrow, 1 fuel, 2 truck, 3 trailer, 4 waypoint, 5 cargo.";
    j["show_blips"]         = show_blips;
    j["blip_size_px"]       = blip_size_px;
    j["player_marker_px"]   = player_marker_px;
    j["blip_atlas_cell_px"] = blip_atlas_cell_px;
    j["blip_atlas_columns"] = blip_atlas_columns;
    j["blip_rotate_icons"]  = blip_rotate_icons;
    j["blip_max_distance"]  = blip_max_distance;

    j["blip_icon_player"]    = blip_icon_player;
    j["blip_icon_waypoint"]  = blip_icon_waypoint;
    j["blip_show_waypoints"] = blip_show_waypoints;

    j["max_capture_size_px"] = max_capture_size_px;
    j["draw_debug_window"]   = draw_debug_window;

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        SM_WARN("Config: cannot write '%s'.", path);
        return false;
    }
    out << j.dump(2) << '\n';
    if (!out) {
        SM_WARN("Config: write to '%s' failed.", path);
        return false;
    }

    SM_INFO("Config saved to '%s'.", path);
    return true;
}

Config& GetConfig()
{
    static Config cfg;
    return cfg;
}

} // namespace snowmap
