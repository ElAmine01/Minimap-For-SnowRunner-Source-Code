#pragma once
/// Lifecycle wrapper around MinHook: init/shutdown plus a staged installer
/// that defers device-dependent hooks until a live D3D11 device exists.

namespace snowmap::hooks {

/// Initialize MinHook and global hook state.
bool InitHookEngine();

/// Disable all hooks and uninitialize MinHook.
void ShutdownHookEngine();

/// Install hooks that only need module base addresses.
bool InstallStaticHooks();

/// Install optional file-system sniffing hooks.
bool InstallFileSniffer();

/// Install hooks that require a live D3D11 device/context.
bool InstallDeviceHooks();

} // namespace snowmap::hooks
