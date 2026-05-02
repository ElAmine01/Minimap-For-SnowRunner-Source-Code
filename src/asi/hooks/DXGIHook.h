#pragma once
//  Hook IDXGISwapChain::Present (vtable index 8) so we can render our
//  ImGui overlay on top of SnowRunner's final frame.
//
//  Why Present and not OMSetRenderTargets / UpdateSubresource:
//    - Present fires exactly once per frame, with the back-buffer already
//      being the active RTV — the safest spot for overlays.
//    - OMSetRenderTargets changes thousands of times per frame (shadow
//      maps, gbuffer, post, UI...). Filtering "the real" RT is fragile —
//      we already tried and saw black frames / instability.
//    - UpdateSubresource is on the ultra-hot path; hooking it was an
//      instant crash in our previous attempt.
//
//  The vtable is discovered by creating a throw-away swap chain during
//  init, reading SwapChain->lpVtbl[8] (Present) and
//  SwapChain->lpVtbl[13] (ResizeBuffers, so we can react to resolution
//  changes), then destroying the probe objects.

namespace snowmap::hooks {

// Returns true on success. After this call, Present/ResizeBuffers are
// trampolined through MinHook and our callbacks run each frame.
bool InstallDXGIHook();
void RemoveDXGIHook();

} // namespace snowmap::hooks
