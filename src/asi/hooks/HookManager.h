#pragma once
//  Lifecycle wrapper around MinHook: Init / Shutdown plus a staged
//  installer that defers "needs a live device" hooks until we actually
//  have one.

namespace snowmap::hooks {

bool InitHookEngine();           // MH_Initialize
void ShutdownHookEngine();       // MH_Uninitialize

// Install hooks that only need a DLL base (none today, but a good seam
// for future pattern-scan hooks on the game exe).
bool InstallStaticHooks();

bool InstallFileSniffer();

// Install hooks that need the D3D11 device/context. Safe to call from
// inside the first Present callback.
bool InstallDeviceHooks();

} // namespace snowmap::hooks
