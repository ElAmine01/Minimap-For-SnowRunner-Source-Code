#pragma once
/// UV lookup for blips.png.
///
/// The sheet is a uniform grid of square cells laid out left to right, top to
/// bottom. Cell size and column count come from options.json so a replacement
/// sheet does not need a rebuild. The shipped sheet is 640x640 with 64px cells:
/// 0 player arrow, 1 fuel, 2 truck, 3 trailer, 4 waypoint pin, 5 cargo.
///
/// Only the kinds the mod can actually place are listed; the other cells stay
/// in the sheet for whoever wires up more sources later.

namespace snowmap::render {

enum class BlipKind
{
    Player = 0,
    Waypoint,
    Count
};

const char* BlipKindName(BlipKind kind);

struct BlipUv
{
    float u0 = 0.f;
    float v0 = 0.f;
    float u1 = 1.f;
    float v1 = 1.f;
};

/// Rect of atlas cell `index`. Falls back to the whole texture when the atlas
/// dimensions or cell settings are unusable, which is visible at a glance in the
/// debug overlay's preview.
BlipUv BlipCellUv(int index, int atlas_w, int atlas_h, int cell_px, int columns);

/// Cell index configured for `kind`.
int BlipCellForKind(BlipKind kind);

/// Fallback colour used when blips.png is missing.
unsigned int BlipKindColor(BlipKind kind);

} // namespace snowmap::render
