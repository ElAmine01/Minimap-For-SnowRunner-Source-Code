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
    zoom_in_key         = GetField(j, "zoom_in_key",         zoom_in_key);
    zoom_out_key        = GetField(j, "zoom_out_key",        zoom_out_key);
    pad_toggle_btn      = static_cast<uint16_t>(GetField<int>(j, "pad_toggle_btn",   static_cast<int>(pad_toggle_btn)));
    pad_reload_btn      = static_cast<uint16_t>(GetField<int>(j, "pad_reload_btn",   static_cast<int>(pad_reload_btn)));
    pad_zoom_in_btn     = static_cast<uint16_t>(GetField<int>(j, "pad_zoom_in_btn",  static_cast<int>(pad_zoom_in_btn)));
    pad_zoom_out_btn    = static_cast<uint16_t>(GetField<int>(j, "pad_zoom_out_btn", static_cast<int>(pad_zoom_out_btn)));
    blips_png_path      = GetField<std::string>(j, "blips_png_path",   blips_png_path);
    max_capture_size_px = GetField(j, "max_capture_size_px", max_capture_size_px);
    draw_debug_window   = GetField(j, "draw_debug_window",   draw_debug_window);

    // Clamp here rather than at the key handler so a hand-edited options.json
    // cannot start the mod outside the stepped zoom range.
    zoom = std::clamp(zoom, kZoomMin, kZoomMax);

    SM_INFO("Config loaded from '%s'.", path);
    return true;
}

Config& GetConfig()
{
    static Config cfg;
    return cfg;
}

} // namespace snowmap
