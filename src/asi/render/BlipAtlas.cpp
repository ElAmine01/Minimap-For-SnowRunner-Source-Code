#include "BlipAtlas.h"

#include "../core/Config.h"

#include <imgui.h>

namespace snowmap::render {

const char* BlipKindName(BlipKind kind)
{
    switch (kind) {
        case BlipKind::Player:   return "Player";
        case BlipKind::Waypoint: return "Waypoint";
        default:                 return "?";
    }
}

BlipUv BlipCellUv(int index, int atlas_w, int atlas_h, int cell_px, int columns)
{
    if (atlas_w <= 0 || atlas_h <= 0 || cell_px <= 0 || columns <= 0 || index < 0) return {};

    const int rows = atlas_h / cell_px;
    const int col  = index % columns;
    const int row  = index / columns;
    if (col * cell_px + cell_px > atlas_w || row >= rows) return {};

    const float w = static_cast<float>(atlas_w);
    const float h = static_cast<float>(atlas_h);
    const float u = static_cast<float>(col * cell_px);
    const float v = static_cast<float>(row * cell_px);
    const float s = static_cast<float>(cell_px);

    return {u / w, v / h, (u + s) / w, (v + s) / h};
}

int BlipCellForKind(BlipKind kind)
{
    const Config& cfg = GetConfig();
    switch (kind) {
        case BlipKind::Waypoint: return cfg.blip_icon_waypoint;
        default:                 return cfg.blip_icon_player;
    }
}

unsigned int BlipKindColor(BlipKind kind)
{
    switch (kind) {
        case BlipKind::Waypoint: return IM_COL32(60, 200, 245, 255);
        default:                 return IM_COL32(235, 64, 52, 255);
    }
}

} // namespace snowmap::render
