# SnowMap — Minimap Mod for SnowRunner

An ASI proxy DLL mod (`dinput8.dll`) providing a real-time minimap overlay for SnowRunner (PC / Steam / Epic). It hooks into DirectX 11 to render a 4K satellite map or a shader-colorized splatmap using Dear ImGui, tracking the player's exact vehicle coordinates.

![Platform](https://img.shields.io/badge/platform-Windows%20x64-blue)
![Tech](https://img.shields.io/badge/tech-C%2B%2B%20%7C%20DX11%20%7C%20ImGui-green)
![Input](https://img.shields.io/badge/input-Keyboard%20%7C%20XInput-orange)

---

## Technical Implementation (Context for AI/Devs)

SnowRunner executable is protected by **Denuvo** (encrypted `.text` section, runtime vtable filling). Static offsets for code or instances are invalid. This mod relies entirely on robust runtime methods:

- **Patch-Proof Offset Discovery:** No memory address is hardcoded. On startup a background scanner walks the game's `.data` section and identifies both globals by what they point at — the `GameSession` by its inline `level_*` string and length field, the `TruckControl` by its RTTI class name `combine::TRUCK_CONTROL` resolved through the vtable's complete object locator. Results are cached in `SnowMap/offsets.cache`, keyed on the executable, and rescanned automatically if a pointer goes stale. A game patch no longer requires a rebuild.
- **Direct RAM Level Detection:** Bypasses I/O hooking entirely. Reads the active level string (e.g., `level_ru_02_02`) directly from the discovered static `GameSession` manager pointer in RAM (currently `SnowRunner.exe + 0x2A543C0`).
- **Map Textures (Cloud Cache):** Asynchronously downloads pre-assembled 4K map PNGs from an external CDN to a local `SnowMap/cache/` directory to bypass proprietary swizzled UI textures (`gfx.pak`).
- **Zero-Hook Vehicle Tracking:** The D3D11 Constant Buffer hook has been eradicated. The mod uses a lightweight background thread to read the active truck's **chassis physics body** directly from RAM via a 4-level static pointer chain: `[SnowRunner.exe + 0x2A8EDD8, discovered at runtime]` → `TruckControl` → `[+0x08]` → `TruckSimNode` → `[+0x60]` → intermediate → `[+0x68]` → `ChassisBody`. Position (world-space X/Y/Z) is read at `+0xC0`. The **Right vector** (local X axis, perpendicular to forward) is read at `+0xB0`; heading is recovered via `atan2f(vec[2], vec[0]) + π/2`. World axes: north = +Z, east = +X.
- **Map Blips:** `SnowMap/blips.png` is a uniform grid of square cells (stock sheet: 640x640, 64px cells - 0 arrow, 1 fuel, 2 truck, 3 trailer, 4 waypoint, 5 cargo). Cell size, column count and the per-kind cell index are all in `options.json`, so a replacement sheet needs no rebuild. Currently drives the player marker and mod-local waypoints; see MAINTENANCE.md for why vehicle/trailer/cargo blips were abandoned and what is still needed for the game's own waypoints.
- **Static UV Math:** Exploits the engine's exact 1:2 meter-to-pixel ratio. The center of the 4K map is always `X=0, Z=0`, making GPU Xform matrix extraction completely obsolete.
- **XInput Controller Support:** Fully supports Xbox and PlayStation controllers (via Steam Input / DS4Windows) natively for seamless minimap navigation and zooming.
- **Overlay:** Uses MinHook on `IDXGISwapChain::Present` to draw a stylized, rotate-aware ImGui minimap completely independently of the game's internal render passes.

---

## Architecture

```text
game.exe
 └─ dinput8.dll (Proxy)
     └─ LoadLibrary("SnowMap.asi")
         ├─ hooks/DXGIHook           -> IDXGISwapChain::Present (ImGui Render)
         ├─ hooks/D3D11Hook          -> ID3D11Device::CreateTexture2D (Map sync signal)
         ├─ hooks/MemoryReader       -> Async thread reading Static RAM pointers (Level, GPS, Yaw)
         ├─ game/MemoryScan          -> Region snapshot + RTTI walk shared by the scanners
         ├─ game/OffsetScanner       -> Background discovery of the engine globals
         ├─ render/MinimapRenderer   -> Orchestrator (ImGui, UV Math, AddImageQuad Rotation, XInput)
         ├─ render/BlipAtlas         -> blips.png cell -> UV lookup
         ├─ render/DebugOverlay      -> F7 diagnostics + live settings editor
         └─ render/MapDownloader     -> WinHTTP CDN background downloader

```

## Controls

| Key | Action |
| --- | --- |
| `F5` | Toggle the minimap |
| `F6` | Reload `SnowMap/options.json` |
| `F7` | Toggle the debug overlay (status, live settings, save config) |
| `F8` | Drop a mod waypoint at the current position |
| `+` / `-` | Zoom in / out |

All bindings are virtual-key codes in `options.json`; `F5`, `F6` and the zoom keys also accept XInput buttons.

## Build Instructions

### Requirements

- Visual Studio 2022
- CMake >= 3.20
- Windows SDK

### Steps

1. Fetch dependencies (MinHook, imgui docking branch, DirectXTK, nlohmann/json) into third_party/.
2. Run build_release.bat (or use CMake directly).
3. Build artifacts (SnowMap.asi, dinput8.dll) will be generated in build/dist/.

### Local Paths (Optional)

If you need a custom MSBuild path or game install location, copy
paths.local.bat.example to paths.local.bat and edit it. This local
file is ignored by git so it will not affect the public repo.

## ⚠️ Antivirus & False Positives

Because this mod uses a proxy DLL (dinput8.dll), reads the game's RAM to track your truck, and connects to the internet to download the map, Windows Defender or your Antivirus may flag it as a virus (e.g., GameHack or Trojan).
This is a 100% False Positive. The mod is completely open-source. To install it, you may need to add the SnowRunner Bin folder to your Antivirus exceptions/exclusions.