#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "DebugOverlay.h"

#include "BlipAtlas.h"
#include "MinimapRenderer.h"
#include "../core/Config.h"
#include "../core/Globals.h"
#include "../game/Waypoints.h"
#include "../game/OffsetScanner.h"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace snowmap::render {
namespace {

bool g_visible     = false;
bool g_initialised = false;

constexpr float kRadToDeg = 57.2957795f;

void SaveConfigNow()
{
    char path[MAX_PATH];
    if (Config::ResolveDefaultPath(path, MAX_PATH)) GetConfig().SaveToFile(path);
}

void ReloadConfigNow()
{
    char path[MAX_PATH];
    if (Config::ResolveDefaultPath(path, MAX_PATH)) GetConfig().LoadFromFile(path);
}

void StatusLine(const char* label, bool ok, const char* yes, const char* no)
{
    ImGui::Text("%s", label);
    ImGui::SameLine(190.f);
    ImGui::TextColored(ok ? ImVec4(0.4f, 0.9f, 0.4f, 1.f) : ImVec4(0.95f, 0.6f, 0.3f, 1.f),
                       "%s", ok ? yes : no);
}

void DrawStatus(const DebugInfo& info)
{
    const game::GameOffsets offsets = game::OffsetScanner::Current();
    const uintptr_t         base    = game::ModuleBase();

    ImGui::Text("Module base       0x%llX", static_cast<unsigned long long>(base));
    ImGui::Text("session_rva       +0x%llX", static_cast<unsigned long long>(offsets.session_rva));
    ImGui::Text("truck_control_rva +0x%llX", static_cast<unsigned long long>(offsets.truck_control_rva));
    ImGui::Text("Level             %s", info.level_id.empty() ? "(none)" : info.level_id.c_str());
    ImGui::Separator();

    StatusLine("Satellite texture", info.satellite_ready, "loaded", "waiting");
    if (info.satellite_ready) ImGui::Text("    %d x %d", info.satellite_w, info.satellite_h);
    StatusLine("Captured game SRV", info.captured_srv, "yes", "no");
    StatusLine("blips.png", info.blips_srv != nullptr, "loaded", "missing");
    if (info.blips_srv) ImGui::Text("    %d x %d", info.blips_w, info.blips_h);

    const char* download = info.download_active ? "downloading"
                         : info.download_failed ? "failed"
                         : info.download_done   ? "done"
                                                : "idle";
    ImGui::Text("Map download      %s", download);
    ImGui::Text("Frame             %.1f FPS (%.2f ms)",
                ImGui::GetIO().Framerate, 1000.f / std::max(ImGui::GetIO().Framerate, 0.001f));
}

void DrawPlayer(const DebugInfo& info)
{
    if (!info.player_valid) {
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1.f), "No player snapshot yet.");
        return;
    }
    ImGui::Text("World   x %.1f  y %.1f  z %.1f",
                info.player_world[0], info.player_world[1], info.player_world[2]);
    ImGui::Text("Heading %.1f deg", info.player_heading * kRadToDeg);
    ImGui::Text("UV      %.4f, %.4f", info.uv_center[0], info.uv_center[1]);
    ImGui::Text("Blips drawn this frame: %d", info.drawn_blips);
}

void DrawWaypoints(const DebugInfo& info)
{
    Config& cfg = GetConfig();

    ImGui::Checkbox("Show waypoints", &cfg.blip_show_waypoints);
    ImGui::Text("Dropped: %zu", game::Waypoints::Count());
    ImGui::SameLine();
    if (ImGui::Button("Clear all")) game::Waypoints::Clear();
    ImGui::TextWrapped("Press the waypoint key (VK 0x%02X) to drop one at the truck. "
                       "These are the mod's own markers; the game's own map waypoint is "
                       "not read yet.", cfg.waypoint_key);

    ImGui::Separator();
    if (ImGui::BeginTable("waypoints", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0.f, 160.f))) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("X / Z");
        ImGui::TableSetupColumn("Dist");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        int index = 0;
        for (const game::Waypoint& point : game::Waypoints::All()) {
            const float dx = point.world[0] - info.player_world[0];
            const float dz = point.world[2] - info.player_world[2];

            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", index++);
            ImGui::TableNextColumn(); ImGui::Text("%.0f / %.0f", point.world[0], point.world[2]);
            ImGui::TableNextColumn(); ImGui::Text("%.0f", std::sqrt(dx * dx + dz * dz));
        }
        ImGui::EndTable();
    }
}

void DrawAppearance()
{
    Config& cfg = GetConfig();

    int anchor = static_cast<int>(cfg.anchor);
    if (ImGui::Combo("Anchor", &anchor, "Top left\0Top right\0Bottom left\0Bottom right\0")) {
        cfg.anchor = static_cast<Config::Anchor>(anchor);
    }
    ImGui::SliderInt("Size (px)", &cfg.size_px, 96, 900);
    ImGui::SliderInt("Offset X", &cfg.position_x, 0, 1200);
    ImGui::SliderInt("Offset Y", &cfg.position_y, 0, 1200);
    ImGui::SliderFloat("Opacity", &cfg.opacity, 0.f, 1.f);

    if (ImGui::SliderFloat("Zoom", &cfg.zoom, Config::kZoomMin, Config::kZoomMax)) {
        cfg.zoom = std::clamp(cfg.zoom, Config::kZoomMin, Config::kZoomMax);
    }
    ImGui::Checkbox("Rotate map with player", &cfg.rotate_with_player);
    ImGui::Checkbox("Show player marker", &cfg.show_player_arrow);

    ImGui::Separator();
    bool overlay = G().overlayVisible.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Minimap visible", &overlay)) {
        G().overlayVisible.store(overlay, std::memory_order_release);
    }
}

void DrawBlipSettings(const DebugInfo& info)
{
    Config& cfg = GetConfig();

    ImGui::Checkbox("Draw blips", &cfg.show_blips);
    ImGui::SliderInt("Blip size (px)", &cfg.blip_size_px, 8, 96);
    ImGui::SliderInt("Player marker (px)", &cfg.player_marker_px, 8, 128);
    ImGui::Checkbox("Rotate blip icons with heading", &cfg.blip_rotate_icons);
    ImGui::SliderFloat("Max distance (0 = all)", &cfg.blip_max_distance, 0.f, 4000.f);

    ImGui::Separator();
    ImGui::Text("Atlas");
    ImGui::SliderInt("Cell size (px)", &cfg.blip_atlas_cell_px, 8, 256);
    ImGui::SliderInt("Columns", &cfg.blip_atlas_columns, 1, 32);
    ImGui::InputInt("Cell: player",   &cfg.blip_icon_player);
    ImGui::InputInt("Cell: waypoint", &cfg.blip_icon_waypoint);

    if (!info.blips_srv) {
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1.f),
                           "blips.png not loaded — check blips_png_path.");
        return;
    }

    ImGui::Separator();
    ImGui::Text("Atlas %d x %d — preview of cells 0-11", info.blips_w, info.blips_h);
    for (int cell = 0; cell < 12; ++cell) {
        const BlipUv uv = BlipCellUv(cell, info.blips_w, info.blips_h,
                                     cfg.blip_atlas_cell_px, cfg.blip_atlas_columns);
        ImGui::BeginGroup();
        ImGui::Image(reinterpret_cast<ImTextureID>(info.blips_srv), ImVec2(48.f, 48.f),
                     ImVec2(uv.u0, uv.v0), ImVec2(uv.u1, uv.v1));
        ImGui::Text("%d", cell);
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("uv (%.4f, %.4f) - (%.4f, %.4f)", uv.u0, uv.v0, uv.u1, uv.v1);
        }
        if (cell % 6 != 5) ImGui::SameLine();
    }
    // A full-sheet rect here means the cell settings were rejected as invalid.
    const BlipUv first = BlipCellUv(0, info.blips_w, info.blips_h,
                                    cfg.blip_atlas_cell_px, cfg.blip_atlas_columns);
    ImGui::Text("Cell 0 uv (%.4f, %.4f) - (%.4f, %.4f)", first.u0, first.v0, first.u1, first.v1);
}

void DrawInput()
{
    const Config& cfg = GetConfig();
    ImGui::Text("Toggle minimap   VK 0x%02X", cfg.toggle_key);
    ImGui::Text("Reload config    VK 0x%02X", cfg.reload_key);
    ImGui::Text("This overlay     VK 0x%02X", cfg.debug_key);
    ImGui::Text("Drop waypoint    VK 0x%02X", cfg.waypoint_key);
    ImGui::Text("Zoom in / out    VK 0x%02X / 0x%02X", cfg.zoom_in_key, cfg.zoom_out_key);
    ImGui::TextWrapped("Key codes are editable in options.json; the overlay does not rebind them.");
}

} // namespace

bool DebugOverlay::Visible()
{
    if (!g_initialised) {
        g_initialised = true;
        g_visible     = GetConfig().draw_debug_window;
    }
    return g_visible;
}

void DebugOverlay::SetVisible(bool visible)
{
    g_initialised = true;
    g_visible     = visible;
}

void DebugOverlay::Toggle() { SetVisible(!Visible()); }

void DebugOverlay::Draw(const DebugInfo& info)
{
    if (!Visible()) {
        ImGui::GetIO().MouseDrawCursor = false;
        return;
    }

    // The game hides the OS cursor while driving, so draw our own or the window
    // is unusable. It also clips the cursor to the viewport while the wheel is
    // captured, which would pin the pointer away from our widgets.
    ImGui::GetIO().MouseDrawCursor = true;
    ClipCursor(nullptr);

    ImGui::SetNextWindowSize(ImVec2(520.f, 620.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60.f, 60.f), ImGuiCond_FirstUseEver);

    bool open = true;
    if (!ImGui::Begin("SnowMap Debug", &open)) {
        ImGui::End();
        if (!open) SetVisible(false);
        return;
    }

    if (ImGui::Button("Save options.json")) SaveConfigNow();
    ImGui::SameLine();
    if (ImGui::Button("Reload from disk")) ReloadConfigNow();
    ImGui::SameLine();
    if (ImGui::Button("Reset defaults")) GetConfig() = Config{};

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen)) DrawStatus(info);
    if (ImGui::CollapsingHeader("Player"))                                 DrawPlayer(info);
    if (ImGui::CollapsingHeader("Waypoints"))                              DrawWaypoints(info);
    if (ImGui::CollapsingHeader("Blips"))                                  DrawBlipSettings(info);
    if (ImGui::CollapsingHeader("Appearance"))                             DrawAppearance();
    if (ImGui::CollapsingHeader("Input"))                                  DrawInput();

    ImGui::End();
    if (!open) SetVisible(false);
}

} // namespace snowmap::render
