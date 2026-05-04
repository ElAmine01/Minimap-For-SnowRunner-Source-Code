#pragma once
/// Hook IDXGISwapChain::Present (vtable index 8) so we can render the ImGui
/// overlay on top of SnowRunner's final frame.
///
/// Why Present and not OMSetRenderTargets / UpdateSubresource:
///   - Present fires exactly once per frame, with the back-buffer already
///     being the active RTV, which is the safest spot for overlays.
///   - OMSetRenderTargets changes thousands of times per frame (shadow
///     maps, gbuffer, post, UI). Filtering the "real" RT is fragile.
///   - UpdateSubresource is on the ultra-hot path; hooking it crashed in
///     previous attempts.
///
/// The vtable is discovered by creating a throw-away swap chain during init,
/// reading SwapChain->lpVtbl[8] (Present) and SwapChain->lpVtbl[13]
/// (ResizeBuffers), then destroying the probe objects.

namespace snowmap::hooks {

/// Install Present/ResizeBuffers hooks. Returns true on success.
bool InstallDXGIHook();
/// Remove Present/ResizeBuffers hooks.
void RemoveDXGIHook();

} // namespace snowmap::hooks
