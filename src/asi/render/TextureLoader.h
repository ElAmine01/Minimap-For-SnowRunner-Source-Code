#pragma once
#include <string>
struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace snowmap::render {

struct LoadedTexture {
    ID3D11ShaderResourceView* srv = nullptr;
    int width = 0;
    int height = 0;
    bool Valid() const { return srv != nullptr; }
    void Release();
};

// On ne garde que la fonction utile : charger un PNG/JPG depuis le disque !
LoadedTexture LoadImageFromDisk(ID3D11Device* dev, const std::string& absolutePath);

} // namespace snowmap::render