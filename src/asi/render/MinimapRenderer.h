#pragma once
#include <string>

struct ID3D11ShaderResourceView;

namespace snowmap::render {

class MinimapRenderer
{
public:
    static void Draw();
    static void OnConfigReloaded();
    static void Shutdown();
    static ID3D11ShaderResourceView* GetActiveMapSRV();

    // Appelé par FileSniffer dès qu'un fichier level_*.pak est ouvert par le moteur.
    // Thread-safe : peut être appelé depuis n'importe quel thread.
    static void OnLevelDetected(const std::string& levelId);
};

} // namespace snowmap::render
