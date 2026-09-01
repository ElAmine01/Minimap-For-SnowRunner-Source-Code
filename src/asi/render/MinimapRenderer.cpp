#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MinimapRenderer.h"
#include "BlipAtlas.h"
#include "DebugOverlay.h"
#include "TextureLoader.h"
#include "MapDownloader.h"
#include "../core/Config.h"
#include "../core/Globals.h"
#include "../core/Logger.h"
#include "../game/MemoryScan.h"
#include "../game/OffsetScanner.h"
#include "../game/PlayerState.h"
#include "../game/Waypoints.h"
#include "../hooks/D3D11Hook.h"

#include <windows.h>
#include <Xinput.h>
#include <shlobj.h>
#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <filesystem>
#include <future>
#include <chrono>
#include <mutex>
#include <atomic>

namespace snowmap::render {
namespace {

LoadedTexture             g_satelliteTexture;
bool                      g_cacheChecked   = false;
ID3D11ShaderResourceView* g_capturedSRV    = nullptr;

static std::future<LoadedTexture> g_loadFuture;

static void FlushLoadFuture() {
    if (!g_loadFuture.valid()) return;
    g_loadFuture.wait();
    LoadedTexture stale = g_loadFuture.get();
    stale.Release();
}

static LoadedTexture      g_blipsTexture;
static bool               g_blipsLoadAttempted = false;

static std::atomic<bool>  g_levelIdReady{false};
static std::mutex         g_detectionMutex;
static std::string        g_pendingLevelId;

// +90 deg to align engine heading with map UV north.
constexpr float kOffset90 = 1.57079632679f;
constexpr float kPi       = 3.14159265f;

using game::scan::SafeRead;

void ProcessCloudCache() {
    if (g_cacheChecked) return;
    if (!g_levelIdReady.load(std::memory_order_acquire)) return;

    if (g_loadFuture.valid()) {
        if (g_loadFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            LoadedTexture result = g_loadFuture.get();
            if (result.Valid()) {
                g_satelliteTexture.Release();
                g_satelliteTexture = result;
                g_cacheChecked = true;
                G().minimapTextureLoaded.store(true, std::memory_order_release);
            }
        }
        return;
    }

    std::string levelId;
    {
        std::lock_guard<std::mutex> lk(g_detectionMutex);
        levelId = g_pendingLevelId;
    }
    if (levelId.empty()) return;

    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* last = strrchr(exePath, '\\');
    if (last) *last = '\0';

    std::string cacheDir = std::string(exePath) + "\\SnowMap\\cache";
    CreateDirectoryA(cacheDir.c_str(), NULL);
    std::string imagePath = cacheDir + "\\" + levelId + ".png";

    auto startAsyncLoad = [&]() {
        ID3D11Device* dev = G().device;
        g_loadFuture = std::async(std::launch::async,
                                  [dev, imagePath] { return LoadImageFromDisk(dev, imagePath, true); });
    };

    if (std::filesystem::exists(imagePath)) {
        startAsyncLoad();
    } else {
        if (!MapDownloader::IsDownloading() && !MapDownloader::IsFailed() && !MapDownloader::IsFinished()) {
            MapDownloader::StartDownload(levelId, imagePath);
        }
        if (MapDownloader::IsFinished()) {
            startAsyncLoad();
        }
    }
}

void RefreshCapture() {
    ID3D11ShaderResourceView* fresh = hooks::AcquireMinimapSRV();
    if (!fresh) return;

    // Ignore our own cached satellite texture; only adopt new game SRVs.
    bool isOurSatellite = (g_satelliteTexture.Valid() && fresh == g_satelliteTexture.srv);
    bool isSameAsCaptured = (g_capturedSRV && fresh == g_capturedSRV);

    if (!isOurSatellite && !isSameAsCaptured) {
        if (g_capturedSRV) g_capturedSRV->Release();
        g_capturedSRV = fresh;
        g_cacheChecked = false;
        MapDownloader::Reset();
    } else {
        if (!isSameAsCaptured) {
            if (g_capturedSRV) g_capturedSRV->Release();
            g_capturedSRV = fresh;
        }
    }
}

void TryLoadBlips() {
    if (g_blipsTexture.Valid() || g_blipsLoadAttempted) return;
    g_blipsLoadAttempted = true;
    char exe[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH)) return;
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = '\0';
    const auto& cfg = GetConfig();
    // Unflipped: the atlas is a sprite sheet, not the north-up satellite map.
    g_blipsTexture = LoadImageFromDisk(G().device, std::string(exe) + "\\" + cfg.blips_png_path, false);
    if (g_blipsTexture.Valid()) {
        SM_INFO("Blips atlas loaded (%d x %d), %d px cells x %d columns.",
                g_blipsTexture.width, g_blipsTexture.height,
                cfg.blip_atlas_cell_px, cfg.blip_atlas_columns);
    } else {
        SM_WARN("Blips atlas '%s' could not be loaded.", cfg.blips_png_path.c_str());
    }
}

static char s_ramLevelId[64] = {};

static bool ReadLevelId(uintptr_t session, char* out, size_t cap) {
    uint32_t length = 0;
    if (!SafeRead(session + game::layout::kSessionNameLength, &length, sizeof(length))) return false;
    if (length < 6 || static_cast<size_t>(length) + 1 > cap) return false;

    // Short ids live in the object's inline buffer; anything longer than the
    // buffer would spill to heap storage the same slot then points at.
    if (length <= game::layout::kMaxInlineNameLength) {
        if (!SafeRead(session + game::layout::kSessionName, out, length + 1)) return false;
    } else {
        uintptr_t heap = 0;
        if (!SafeRead(session + game::layout::kSessionName, &heap, sizeof(heap)) || !heap) return false;
        if (!SafeRead(heap, out, length + 1)) return false;
    }

    out[cap - 1] = '\0';
    return out[length] == '\0' && strncmp(out, "level_", 6) == 0;
}

static void PollLevelIdFromRam() {
    // Throttle RAM reads to avoid hammering the process each frame.
    static int s_tick = 0;
    if ((s_tick++ % 60) != 0) return;

    const uintptr_t global = game::OffsetScanner::SessionGlobal();
    if (!global) return;

    uintptr_t session = 0;
    if (!SafeRead(global, &session, sizeof(session)) || !session) {
        game::OffsetScanner::ReportSessionResult(false);
        return;
    }

    char levelId[64] = {};
    if (!ReadLevelId(session, levelId, sizeof(levelId))) {
        game::OffsetScanner::ReportSessionResult(false);
        return;
    }
    game::OffsetScanner::ReportSessionResult(true);

    if (strcmp(levelId, s_ramLevelId) != 0) {
        memcpy(s_ramLevelId, levelId, sizeof(s_ramLevelId));
        MinimapRenderer::OnLevelDetected(std::string(levelId));
    }
}

static void PollVehicleFromRam() {
    const uintptr_t global = game::OffsetScanner::TruckControlGlobal();
    if (!global) return;

    // A null chain is expected in menus and in the garage, so only a loaded
    // level counts as evidence that the offset itself went bad.
    const bool levelActive = s_ramLevelId[0] != '\0' && !strstr(s_ramLevelId, "main_menu");
    const auto fail = [levelActive] { game::OffsetScanner::ReportTruckResult(false, levelActive); };

    // Chain: TruckControl -> [+0x08] -> tsn -> [+0x60] -> mid -> [+0x68] -> chassis_body
    uintptr_t truck_control = 0;
    if (!SafeRead(global, &truck_control, sizeof(truck_control)) || !truck_control) { fail(); return; }

    uintptr_t tsn = 0;
    if (!SafeRead(truck_control + game::layout::kTruckSimNode, &tsn, sizeof(tsn)) || !tsn) { fail(); return; }

    uintptr_t mid = 0;
    if (!SafeRead(tsn + game::layout::kSimNodeChild, &mid, sizeof(mid)) || !mid) { fail(); return; }

    uintptr_t chassis = 0;
    if (!SafeRead(mid + game::layout::kChildChassisBody, &chassis, sizeof(chassis)) || !chassis) { fail(); return; }

    float fwd[3];
    if (!SafeRead(chassis + game::layout::kBodyRightVector, fwd, sizeof(fwd))) { fail(); return; }

    float pos[3];
    if (!SafeRead(chassis + game::layout::kBodyWorldPosition, pos, sizeof(pos))) { fail(); return; }
    if (!std::isfinite(pos[0]) || !std::isfinite(pos[2])) { fail(); return; }

    game::OffsetScanner::ReportTruckResult(true, levelActive);

    // +0xB0 is the Right vector (local X), not Forward — add π/2 to recover truck heading.
    const float heading = std::atan2f(fwd[2], fwd[0]) + kOffset90;
    game::PlayerState::UpdateCamera(pos[0], pos[1], pos[2], heading);
}

ImVec2 AnchorToScreenPos(int pad_x, int pad_y, int size) {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x;
    const float H = io.DisplaySize.y;
    switch (GetConfig().anchor) {
        case Config::TopLeft:     return ImVec2(float(pad_x), float(pad_y));
        case Config::TopRight:    return ImVec2(W - size - pad_x, float(pad_y));
        case Config::BottomLeft:  return ImVec2(float(pad_x), H - size - pad_y);
        case Config::BottomRight: return ImVec2(W - size - pad_x, H - size - pad_y);
    }
    return ImVec2(float(pad_x), float(pad_y));
}

void ReloadConfigFromDisk() {
    char path[MAX_PATH];
    if (Config::ResolveDefaultPath(path, MAX_PATH)) GetConfig().LoadFromFile(path);
}

static void PollInput() {
    using clock = std::chrono::steady_clock;
    using ms    = std::chrono::milliseconds;
    Config& cfg = GetConfig();

    static bool              s_toggleDown   = false;
    static bool              s_reloadDown   = false;
    static bool              s_debugDown    = false;
    static bool              s_waypointDown = false;
    static clock::time_point s_lastToggle{};
    static clock::time_point s_lastReload{};
    static clock::time_point s_lastDebug{};
    static clock::time_point s_lastWaypoint{};
    static clock::time_point s_lastZoomIn{};
    static clock::time_point s_lastZoomOut{};

    const auto now  = clock::now();
    XINPUT_STATE state{};
    const bool padConnected = (XInputGetState(0, &state) == ERROR_SUCCESS);

    auto isActionDown = [&](int vk, uint16_t padBtn) {
        const bool keyDown = (GetAsyncKeyState(vk & 0xFF) & 0x8000) != 0;
        const bool padDown = padConnected && padBtn != 0 && (state.Gamepad.wButtons & padBtn) != 0;
        return keyDown || padDown;
    };

    auto debouncedPress = [&](int vk, uint16_t padBtn, bool& wasDown, clock::time_point& last) {
        const bool down = isActionDown(vk, padBtn);
        bool fired = false;
        if (down && !wasDown && now - last > ms(200)) {
            fired = true; last = now;
        }
        wasDown = down;
        return fired;
    };

    auto debouncedHold = [&](int vk, uint16_t padBtn, clock::time_point& last) {
        if (isActionDown(vk, padBtn) && now - last > ms(200)) {
            last = now; return true;
        }
        return false;
    };

    if (debouncedPress(cfg.toggle_key, cfg.pad_toggle_btn, s_toggleDown, s_lastToggle)) {
        const bool vis = G().overlayVisible.load(std::memory_order_relaxed);
        G().overlayVisible.store(!vis, std::memory_order_release);
    }

    if (debouncedPress(cfg.reload_key, cfg.pad_reload_btn, s_reloadDown, s_lastReload))
        ReloadConfigFromDisk();

    if (debouncedPress(cfg.debug_key, 0, s_debugDown, s_lastDebug))
        DebugOverlay::Toggle();

    if (debouncedPress(cfg.waypoint_key, cfg.pad_waypoint_btn, s_waypointDown, s_lastWaypoint)) {
        const auto snap = game::PlayerState::Current();
        if (snap.valid) {
            game::Waypoints::Add(snap.eye_world[0], snap.eye_world[1], snap.eye_world[2]);
            SM_INFO("Waypoint dropped at %.1f / %.1f.", snap.eye_world[0], snap.eye_world[2]);
        }
    }

    if (debouncedHold(cfg.zoom_in_key, cfg.pad_zoom_in_btn, s_lastZoomIn))
        cfg.zoom = std::min(cfg.zoom + Config::kZoomStep, Config::kZoomMax);

    if (debouncedHold(cfg.zoom_out_key, cfg.pad_zoom_out_btn, s_lastZoomOut))
        cfg.zoom = std::max(cfg.zoom - Config::kZoomStep, Config::kZoomMin);
}

// ---------------------------------------------------------------------------
//  Blips
// ---------------------------------------------------------------------------

/// Everything needed to place a world position on the minimap. Derived from the
/// same quantities the map quad is drawn with, so blips and terrain cannot
/// drift apart.
struct Projection
{
    ImVec2 center{};
    ImVec2 half_extent{};   ///< half the minimap in screen pixels
    float  world_w  = 0.f;  ///< world units covered by half the texture width
    float  world_h  = 0.f;
    float  half_u   = 0.5f;
    float  half_v   = 0.5f;
    float  cos_rot  = 1.f;
    float  sin_rot  = 0.f;
    bool   rotate   = false;
    bool   world_ok = false;

    /// Screen position for a world XZ, relative to the player at the centre.
    ImVec2 Project(float dx, float dz) const
    {
        const float uv_x = dx / world_w;
        const float uv_y = -dz / world_h;

        float qx = uv_x / half_u;
        float qy = uv_y / half_v;

        if (rotate) {
            // The map quad maps screen unit q to uv R(rot)*q, so going the other
            // way needs the inverse rotation.
            const float rx = qx * cos_rot + qy * sin_rot;
            const float ry = -qx * sin_rot + qy * cos_rot;
            qx = rx;
            qy = ry;
        }
        return ImVec2(center.x + qx * half_extent.x, center.y + qy * half_extent.y);
    }
};

/// Icon rotation for an entity.
///
/// Screen up is world +Z and screen right is +X, so a truck facing heading h
/// points along (cos h, -sin h) in screen space. An atlas icon drawn pointing up
/// therefore needs a screen rotation of pi/2 - h, and DrawBlipIcon rotates by
/// -a, giving a = h - pi/2. When the map rotates with the player, every angle is
/// relative to the player's own heading instead.
float BlipAngle(float entity_heading, float player_heading, bool rotate_with_player)
{
    return rotate_with_player ? (entity_heading - player_heading)
                              : (entity_heading - kOffset90);
}

void DrawBlipIcon(ImDrawList* dl, ImVec2 at, float size, float angle, BlipKind kind)
{
    const Config& cfg  = GetConfig();
    const float   half = size * 0.5f;

    const float ca = cosf(angle);
    const float sa = -sinf(angle);
    auto corner = [&](float dx, float dy) -> ImVec2 {
        return ImVec2(at.x + dx * ca - dy * sa, at.y + dx * sa + dy * ca);
    };

    if (g_blipsTexture.Valid()) {
        const int    cell = BlipCellForKind(kind);
        const BlipUv uv   = BlipCellUv(cell, g_blipsTexture.width, g_blipsTexture.height,
                                       cfg.blip_atlas_cell_px, cfg.blip_atlas_columns);

        // One line per kind, so a sheet that samples wrong is diagnosable from
        // the log instead of by squinting at a 30px quad.
        static bool s_logged[static_cast<int>(BlipKind::Count)] = {};
        const int   slot = static_cast<int>(kind);
        if (slot >= 0 && slot < static_cast<int>(BlipKind::Count) && !s_logged[slot]) {
            s_logged[slot] = true;
            SM_INFO("Blip %s -> cell %d, uv (%.4f, %.4f)-(%.4f, %.4f).",
                    BlipKindName(kind), cell, uv.u0, uv.v0, uv.u1, uv.v1);
        }

        dl->AddImageQuad(reinterpret_cast<ImTextureID>(g_blipsTexture.srv),
                         corner(-half, -half), corner(half, -half),
                         corner(half, half),   corner(-half, half),
                         ImVec2(uv.u0, uv.v0), ImVec2(uv.u1, uv.v0),
                         ImVec2(uv.u1, uv.v1), ImVec2(uv.u0, uv.v1),
                         IM_COL32_WHITE);
        return;
    }

    dl->AddCircleFilled(at, half * 0.6f, BlipKindColor(kind));
    dl->AddCircle(at, half * 0.6f, IM_COL32(20, 20, 20, 220), 0, 1.5f);
}

int DrawWaypointBlips(ImDrawList* dl, const Projection& proj, const game::PlayerSnapshot& snap,
                      const ImVec2& wMin, const ImVec2& wMax)
{
    const Config& cfg = GetConfig();
    if (!cfg.show_blips || !cfg.blip_show_waypoints || !proj.world_ok) return 0;

    const float size   = static_cast<float>(std::max(4, cfg.blip_size_px));
    const float margin = size;
    int         drawn  = 0;

    for (const game::Waypoint& point : game::Waypoints::All()) {
        const float dx = point.world[0] - snap.eye_world[0];
        const float dz = point.world[2] - snap.eye_world[2];

        if (cfg.blip_max_distance > 0.f) {
            if (dx * dx + dz * dz > cfg.blip_max_distance * cfg.blip_max_distance) continue;
        }

        const ImVec2 at = proj.Project(dx, dz);
        if (at.x < wMin.x - margin || at.x > wMax.x + margin ||
            at.y < wMin.y - margin || at.y > wMax.y + margin) continue;

        // The pin is drawn upright: a marker has no heading to point along.
        DrawBlipIcon(dl, at, size, 0.f, BlipKind::Waypoint);
        ++drawn;
    }
    return drawn;
}

void DrawPlayerMarker(ImDrawList* dl, const ImVec2& center, float heading)
{
    const Config& cfg = GetConfig();
    // Same convention as every other blip, so the player cannot drift out of
    // sync with the traffic around it.
    const float   a   = BlipAngle(heading, heading, cfg.rotate_with_player);

    if (g_blipsTexture.Valid()) {
        DrawBlipIcon(dl, center, static_cast<float>(std::max(8, cfg.player_marker_px)), a,
                     BlipKind::Player);
        return;
    }

    // Vector fallback so the player stays locatable if blips.png is missing.

    const float theta = -a;
    const float ct = cosf(theta), st = sinf(theta);
    auto marker = [&](float dx, float dy) -> ImVec2 {
        return ImVec2(center.x + dx * ct - dy * st, center.y + dx * st + dy * ct);
    };
    const ImVec2 tip   = marker( 0.0f, -13.0f);
    const ImVec2 left  = marker(-9.0f,   9.0f);
    const ImVec2 right = marker( 9.0f,   9.0f);
    const ImVec2 notch = marker( 0.0f,   4.0f);

    dl->AddTriangleFilled(tip, right, notch, IM_COL32(235, 64, 52, 255));
    dl->AddTriangleFilled(tip, notch, left,  IM_COL32(255, 120, 110, 255));
    dl->AddTriangle(tip, right, left, IM_COL32(20, 20, 20, 220), 1.5f);
}

DebugInfo BuildDebugInfo() {
    DebugInfo info;
    {
        std::lock_guard<std::mutex> lk(g_detectionMutex);
        info.level_id = g_pendingLevelId;
    }
    info.satellite_ready = g_satelliteTexture.Valid();
    info.satellite_w     = g_satelliteTexture.width;
    info.satellite_h     = g_satelliteTexture.height;
    info.captured_srv    = g_capturedSRV != nullptr;
    info.download_active = MapDownloader::IsDownloading();
    info.download_failed = MapDownloader::IsFailed();
    info.download_done   = MapDownloader::IsFinished();
    info.blips_srv       = g_blipsTexture.srv;
    info.blips_w         = g_blipsTexture.width;
    info.blips_h         = g_blipsTexture.height;

    const auto snap = game::PlayerState::Current();
    info.player_valid   = snap.valid;
    info.player_world[0] = snap.eye_world[0];
    info.player_world[1] = snap.eye_world[1];
    info.player_world[2] = snap.eye_world[2];
    info.player_heading  = snap.heading_rad;
    return info;
}

} // namespace

void MinimapRenderer::Draw() {
    PollInput();

    PollLevelIdFromRam();
    PollVehicleFromRam();

    TryLoadBlips();

    const bool overlayVisible = G().overlayVisible.load(std::memory_order_acquire);

    bool levelReady = false;
    {
        std::lock_guard<std::mutex> lk(g_detectionMutex);
        levelReady = !g_pendingLevelId.empty() &&
                     g_pendingLevelId.find("main_menu") == std::string::npos;
    }

    if (levelReady) {
        RefreshCapture();
        ProcessCloudCache();
    }

    DebugInfo debug = BuildDebugInfo();

    const Config& cfg = GetConfig();
    auto snap = game::PlayerState::Current();
    ID3D11ShaderResourceView* srv = g_satelliteTexture.Valid() ? g_satelliteTexture.srv : g_capturedSRV;

    if (!overlayVisible || !levelReady || !srv) {
        DebugOverlay::Draw(debug);
        return;
    }

    // Force a square minimap to avoid any UI stretch.
    const int size_w = std::max(64, cfg.size_px);
    const int size_h = size_w;

    ImGui::SetNextWindowPos(AnchorToScreenPos(cfg.position_x, cfg.position_y, size_w), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(float(size_w), float(size_h)), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(cfg.opacity);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoInputs;

    if (!snap.valid) {
        ImGui::Begin("##SnowMapFallback", nullptr, flags);
        ImGui::End();
        DebugOverlay::Draw(debug);
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    if (ImGui::Begin("##SnowMapMinimap", nullptr, flags)) {
        const ImVec2 contentSz = ImGui::GetContentRegionAvail();
        const ImVec2 wMin      = ImGui::GetWindowPos();
        const ImVec2 wMax      = ImVec2(wMin.x + contentSz.x, wMin.y + contentSz.y);
        ImDrawList*  dl        = ImGui::GetWindowDrawList();
        ImGui::PushClipRect(wMin, wMax, true);

        ImVec2 uvCenter(0.5f, 0.5f);
        float half_u = 0.5f, half_v = 0.5f;

        Projection proj;
        proj.center      = ImVec2(wMin.x + contentSz.x * 0.5f, wMin.y + contentSz.y * 0.5f);
        proj.half_extent = ImVec2(contentSz.x * 0.5f, contentSz.y * 0.5f);
        proj.rotate      = cfg.rotate_with_player;

        if (g_satelliteTexture.Valid() && g_satelliteTexture.width > 0) {
            const float world_w = g_satelliteTexture.width  * 0.5f;
            const float world_h = g_satelliteTexture.height * 0.5f;
            uvCenter.x = (snap.eye_world[0] / world_w) + 0.5f;
            // Flip Z so north stays up in texture UV space.
            uvCenter.y = 0.5f - (snap.eye_world[2] / world_h);

            // Aspect ratio compensation to avoid texture stretching.
            const float aspect = float(g_satelliteTexture.width) / float(g_satelliteTexture.height);
            const float span_u = 1.0f / std::max(cfg.zoom, 0.01f);
            const float span_v = span_u * aspect;

            half_u = span_u * 0.5f;
            half_v = span_v * 0.5f;

            proj.world_w  = world_w;
            proj.world_h  = world_h;
            proj.world_ok = true;
        }

        proj.half_u = half_u;
        proj.half_v = half_v;

        // Convert engine heading to UI rotation and align north.
        const float rot = -snap.heading_rad + kOffset90 + kPi;
        proj.cos_rot = cosf(rot);
        proj.sin_rot = sinf(rot);

        // Rotate first in square space, then apply aspect correction on UV axes.
        const ImVec2 q0(-1.0f, -1.0f);
        const ImVec2 q1( 1.0f, -1.0f);
        const ImVec2 q2( 1.0f,  1.0f);
        const ImVec2 q3(-1.0f,  1.0f);

        auto toUv = [&](const ImVec2& p) -> ImVec2 {
            return ImVec2(uvCenter.x + p.x * half_u, uvCenter.y + p.y * half_v);
        };

        // Opacity has to reach the image itself; the window background alpha
        // only dims what shows through where the map is not drawn.
        const int    alpha = static_cast<int>(std::clamp(cfg.opacity, 0.f, 1.f) * 255.f + 0.5f);
        const ImU32  tint  = IM_COL32(255, 255, 255, alpha);

        if (cfg.rotate_with_player) {
            const ImVec2 p0 = wMin, p1 = ImVec2(wMax.x, wMin.y), p2 = wMax, p3 = ImVec2(wMin.x, wMax.y);

            const float ca = proj.cos_rot;
            const float sa = proj.sin_rot;
            auto rotUnit = [&](const ImVec2& p) -> ImVec2 {
                return ImVec2(p.x * ca - p.y * sa, p.x * sa + p.y * ca);
            };

            dl->AddImageQuad(reinterpret_cast<ImTextureID>(srv), p0, p1, p2, p3,
                             toUv(rotUnit(q0)), toUv(rotUnit(q1)), toUv(rotUnit(q2)), toUv(rotUnit(q3)),
                             tint);
        } else {
            // Non-rotating map mode.
            dl->AddImage(reinterpret_cast<ImTextureID>(srv), wMin, wMax,
                         toUv(q0), toUv(q2), tint);
        }

        debug.uv_center[0] = uvCenter.x;
        debug.uv_center[1] = uvCenter.y;
        debug.drawn_blips  = DrawWaypointBlips(dl, proj, snap, wMin, wMax);

        if (cfg.show_player_arrow) DrawPlayerMarker(dl, proj.center, snap.heading_rad);

        ImGui::PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleVar();

    DebugOverlay::Draw(debug);
}

void MinimapRenderer::OnConfigReloaded() {
    ReloadConfigFromDisk();
    FlushLoadFuture();
    g_blipsTexture.Release();
    g_blipsLoadAttempted = false;
    g_satelliteTexture.Release();
    g_cacheChecked = false;
    MapDownloader::Reset();
}

void MinimapRenderer::Shutdown() {
    FlushLoadFuture();
    g_satelliteTexture.Release();
    g_cacheChecked = false;
    {
        std::lock_guard<std::mutex> lk(g_detectionMutex);
        g_pendingLevelId.clear();
    }
    g_levelIdReady.store(false, std::memory_order_release);
    if (g_capturedSRV) g_capturedSRV->Release();
    g_blipsTexture.Release();
}

void MinimapRenderer::OnLevelDetected(const std::string& levelId) {
    if (levelId.empty()) return;
    {
        std::lock_guard<std::mutex> lk(g_detectionMutex);
        if (g_pendingLevelId == levelId) return;
        g_pendingLevelId = levelId;
    }
    g_cacheChecked = false;
    g_levelIdReady.store(true, std::memory_order_release);
    MapDownloader::Reset();
    game::PlayerState::ResetOffsets();
    game::Waypoints::OnLevelChanged();
}

ID3D11ShaderResourceView* MinimapRenderer::GetActiveMapSRV() { return g_satelliteTexture.srv; }

} // namespace snowmap::render
