#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MinimapRenderer.h"
#include "TextureLoader.h"
#include "MapDownloader.h"
#include "../core/Config.h"
#include "../core/Globals.h"
#include "../core/Logger.h"
#include "../game/PlayerState.h"
#include "../hooks/D3D11Hook.h"

#include <windows.h>
#include <Xinput.h>
#include <shlobj.h> 
#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
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

static LoadedTexture      g_arrowTexture;
static bool               g_arrowLoadAttempted = false;

static std::atomic<bool>  g_levelIdReady{false};
static std::mutex         g_detectionMutex;
static std::string        g_pendingLevelId;

// +90 deg to align engine heading with map UV north.
constexpr float kOffset90 = 1.57079632679f;

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
        g_loadFuture = std::async(std::launch::async, LoadImageFromDisk, dev, imagePath);
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
    g_blipsTexture = LoadImageFromDisk(G().device, std::string(exe) + "\\" + cfg.blips_png_path);
}

void TryLoadArrow() {
    if (g_arrowTexture.Valid() || g_arrowLoadAttempted) return;
    g_arrowLoadAttempted = true;
    char exe[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH)) return;
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = '\0';
    g_arrowTexture = LoadImageFromDisk(G().device, std::string(exe) + "\\SnowMap\\arrow.png");
}

static char s_ramLevelId[64] = {};

static bool SafeRead(uintptr_t addr, void* dst, size_t size) {
    if (!addr) return false;
    SIZE_T n = 0;
    return ReadProcessMemory(GetCurrentProcess(),
                             reinterpret_cast<LPCVOID>(addr), dst, size, &n)
           && n == size;
}

static void PollLevelIdFromRam() {
    // Throttle RAM reads to avoid hammering the process each frame.
    static int s_tick = 0;
    if ((s_tick++ % 60) != 0) return;

    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!base) return;

    // GameSession pointer (static) holds a std::string with the level id.
    uintptr_t session = 0;
    if (!SafeRead(base + 0x2A4E038, &session, sizeof(session)) || !session) return;

    char buf[16] = {};
    if (!SafeRead(session + 0x18, buf, 16)) return;

    char levelId[64] = {};

    // std::string uses SSO: inline buffer or a pointer to heap storage.
    if (strncmp(buf, "level_", 6) == 0) {
        strncpy(levelId, buf, 15);
        levelId[15] = '\0';
    } 
    else {
        uintptr_t heapPtr = *reinterpret_cast<uintptr_t*>(buf);
        if (heapPtr < 0x10000) return; 
        if (!SafeRead(heapPtr, levelId, 32)) return;
        levelId[31] = '\0';
        if (strncmp(levelId, "level_", 6) != 0) return;
    }

    if (strcmp(levelId, s_ramLevelId) != 0) {
        memcpy(s_ramLevelId, levelId, sizeof(s_ramLevelId));
        MinimapRenderer::OnLevelDetected(std::string(levelId));
    }
}

static void PollVehicleFromRam() {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!base) return;

    // Chain: [base + 0x2A876A8] -> truck_control -> [+0x08] -> tsn -> [+0x60] -> mid -> [+0x68] -> chassis_body
    uintptr_t truck_control = 0;
    if (!SafeRead(base + 0x2A876A8, &truck_control, sizeof(truck_control)) || !truck_control) return;

    uintptr_t tsn = 0;
    if (!SafeRead(truck_control + 0x08, &tsn, sizeof(tsn)) || !tsn) return;

    uintptr_t mid = 0;
    if (!SafeRead(tsn + 0x60, &mid, sizeof(mid)) || !mid) return;

    uintptr_t chassis = 0;
    if (!SafeRead(mid + 0x68, &chassis, sizeof(chassis)) || !chassis) return;

    // Husky body State C: forward vector at +0xB0, world position at +0xC0.
    float fwd[3];
    if (!SafeRead(chassis + 0xB0, fwd, sizeof(fwd))) return;

    float pos[3];
    if (!SafeRead(chassis + 0xC0, pos, sizeof(pos))) return;
    if (!std::isfinite(pos[0]) || !std::isfinite(pos[2])) return;

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

    static bool              s_toggleDown  = false;
    static bool              s_reloadDown  = false;
    static clock::time_point s_lastToggle{};
    static clock::time_point s_lastReload{};
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

    if (debouncedHold(cfg.zoom_in_key, cfg.pad_zoom_in_btn, s_lastZoomIn))
        cfg.zoom = std::min(cfg.zoom + 0.5f, 150.0f);

    if (debouncedHold(cfg.zoom_out_key, cfg.pad_zoom_out_btn, s_lastZoomOut))
        cfg.zoom = std::max(cfg.zoom - 0.5f, 1.0f);
}

} // namespace

void MinimapRenderer::Draw() {
    PollInput();

    if (!G().overlayVisible.load(std::memory_order_acquire)) return;

    PollLevelIdFromRam();
    PollVehicleFromRam();

    {
        std::lock_guard<std::mutex> lk(g_detectionMutex);
        if (g_pendingLevelId.empty() || g_pendingLevelId.find("main_menu") != std::string::npos) return;
    }

    RefreshCapture();
    TryLoadBlips();
    TryLoadArrow();
    ProcessCloudCache();

    const Config& cfg = GetConfig();
    auto snap = game::PlayerState::Current();
    ID3D11ShaderResourceView* srv = g_satelliteTexture.Valid() ? g_satelliteTexture.srv : g_capturedSRV;

    if (!srv) {
        if (cfg.draw_debug_window) {
            ImGui::Begin("SnowMap Debug");
            ImGui::TextColored(ImVec4(1,1,0,1), "Attente texture...");
            ImGui::End();
        }
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
        }

        // Convert engine heading to UI rotation and align north.
        const float rot = -snap.heading_rad + kOffset90 + 3.14159265f;

        // Rotate first in square space, then apply aspect correction on UV axes.
        const ImVec2 q0(-1.0f, -1.0f);
        const ImVec2 q1( 1.0f, -1.0f);
        const ImVec2 q2( 1.0f,  1.0f);
        const ImVec2 q3(-1.0f,  1.0f);

        auto toUv = [&](const ImVec2& p) -> ImVec2 {
            return ImVec2(uvCenter.x + p.x * half_u, uvCenter.y + p.y * half_v);
        };

        if (cfg.rotate_with_player) {
            const ImVec2 p0 = wMin, p1 = ImVec2(wMax.x, wMin.y), p2 = wMax, p3 = ImVec2(wMin.x, wMax.y);

            const float ca = cosf(rot);
            const float sa = sinf(rot);
            auto rotUnit = [&](const ImVec2& p) -> ImVec2 {
                return ImVec2(p.x * ca - p.y * sa, p.x * sa + p.y * ca);
            };

            dl->AddImageQuad(reinterpret_cast<ImTextureID>(srv), p0, p1, p2, p3,
                             toUv(rotUnit(q0)), toUv(rotUnit(q1)), toUv(rotUnit(q2)), toUv(rotUnit(q3)),
                             IM_COL32_WHITE);
        } else {
            // Non-rotating map mode.
            ImVec2 uv0 = toUv(q0); // Top Left
            ImVec2 uv1 = toUv(q2); // Bottom Right
            ImGui::Image(reinterpret_cast<ImTextureID>(srv), contentSz, uv0, uv1);
        }

        if (cfg.show_player_arrow) {
            const float cx = wMin.x + contentSz.x * 0.5f;
            const float cy = wMin.y + contentSz.y * 0.5f;
            constexpr float kPi = 3.14159265f;
            const float a = cfg.rotate_with_player ? 3.14159265f : -rot;

            if (g_arrowTexture.Valid()) {
                const float half = 16.0f;
                const float ca = cosf(a), sa = -sinf(a);
                auto rotArrow = [&](float dx, float dy) -> ImVec2 {
                    return ImVec2(cx + dx * ca - dy * sa, cy + dx * sa + dy * ca);
                };
                dl->AddImageQuad(reinterpret_cast<ImTextureID>(g_arrowTexture.srv),
                                 rotArrow(-half, -half), rotArrow( half, -half),
                                 rotArrow( half,  half), rotArrow(-half,  half),
                                 ImVec2(0,0), ImVec2(1,0), ImVec2(1,1), ImVec2(0,1), IM_COL32_WHITE);
            }
        }

        ImGui::PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void MinimapRenderer::OnConfigReloaded() {
    ReloadConfigFromDisk();
    FlushLoadFuture();
    g_blipsTexture.Release();
    g_blipsLoadAttempted = false;
    g_arrowTexture.Release();
    g_arrowLoadAttempted = false;
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
    g_arrowTexture.Release();
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
}

ID3D11ShaderResourceView* MinimapRenderer::GetActiveMapSRV() { return g_satelliteTexture.srv; }

} // namespace snowmap::render