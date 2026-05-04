#pragma once
#include <cstdint>

namespace snowmap::game {

/// Thread-safe snapshot of player camera state.
struct PlayerSnapshot
{
    float eye_world[3] = {0, 0, 0};
    float heading_rad  = 0.f;
    bool  valid        = false;
    uint64_t stamp     = 0;
};

/// Centralized access to the latest camera position and heading.
class PlayerState
{
public:
    /// Return the most recent snapshot.
    static PlayerSnapshot Current();
    /// Update camera state from RAM polling.
    static void UpdateCamera(float x, float y, float z, float heading_rad);
    /// Reset validity after level changes or device resets.
    static void ResetOffsets();
};

} // namespace snowmap::game
