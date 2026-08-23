#pragma once
#include <string>
struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace snowmap::render {

/// GPU texture wrapper loaded from disk via WIC.
struct LoadedTexture {
    ID3D11ShaderResourceView* srv = nullptr;
    int width = 0;
    int height = 0;
    /// True if the SRV handle is valid.
    bool Valid() const { return srv != nullptr; }
    /// Release the SRV and reset dimensions.
    void Release();
};

/// Load a PNG/JPG from disk into a D3D11 shader resource view.
///
/// `flip_vertically` exists for the satellite map, whose UV math wants north at
/// row 0. Sprite sheets must be loaded unflipped or their cell rows move.
LoadedTexture LoadImageFromDisk(ID3D11Device* dev, const std::string& absolutePath,
                                bool flip_vertically = true);

} // namespace snowmap::render