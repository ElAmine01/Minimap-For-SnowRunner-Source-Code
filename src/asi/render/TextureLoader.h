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
LoadedTexture LoadImageFromDisk(ID3D11Device* dev, const std::string& absolutePath);

} // namespace snowmap::render