#pragma once
#include <cstdint>

namespace snowmap::game {

struct PlayerSnapshot
{
    float eye_world[3] = {0, 0, 0};
    float heading_rad  = 0.f;
    bool  valid        = false;
    uint64_t stamp     = 0;
};

class PlayerState
{
public:
    static PlayerSnapshot Current();
    static void UpdateCamera(float x, float y, float z, float heading_rad);
    static void ResetOffsets();
};

} // namespace snowmap::game
