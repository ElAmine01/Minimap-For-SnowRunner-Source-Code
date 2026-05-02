#pragma once
#include <cstdint>
//  Hook ID3D11Device::CreateTexture2D (vtable index 5) to intercept the
//  minimap texture the game uploads at level load time.
//
//  The game stores its map textures in a proprietary .pct (TCIP) format
//  inside .pak archives. Reading them directly is not feasible. Instead we
//  let the engine load and decode the texture itself, and we steal a
//  reference to the resulting ID3D11ShaderResourceView the moment
//  CreateTexture2D is called with the matching dimensions/format.
//
//  Filter criteria for g_txMinimapMapBase:
//    - Format: DXGI_FORMAT_BC7_UNORM or BC7_UNORM_SRGB
//    - Width == Height (square)
//    - Width in {1024, 1368, 2048}  — observed across versions
//    - BindFlags includes SHADER_RESOURCE
//    - MipLevels > 0 (generated mipchain = intentional texture, not temp RT)
//
//  Thread-safety: the captured SRV pointer is published atomically.

struct ID3D11ShaderResourceView;

namespace snowmap::hooks {

bool InstallD3D11Hook();       // call once, after device is captured
void RemoveD3D11Hook();

// Returns the captured minimap SRV (nullptr if not yet captured).
// The returned pointer is AddRef'd — caller must Release().
ID3D11ShaderResourceView* AcquireMinimapSRV();

// Returns the pixel dimensions of the currently captured minimap texture.
// Both outputs are 0 if no texture has been captured yet.
void GetMinimapSRVSize(uint32_t& outW, uint32_t& outH);

} // namespace snowmap::hooks
