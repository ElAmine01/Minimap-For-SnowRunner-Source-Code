#pragma once
/// Player-dropped map markers, in world coordinates.
///
/// These are the mod's own, not the game's. Reading the game's waypoint list is
/// documented as unfinished in MAINTENANCE.md.

#include <cstddef>
#include <vector>

namespace snowmap::game {

struct Waypoint
{
    float world[3] = {0.f, 0.f, 0.f};
};

class Waypoints
{
public:
    static void Add(float x, float y, float z);
    static void Clear();
    static void OnLevelChanged();

    static std::vector<Waypoint> All();
    static size_t                Count();
};

} // namespace snowmap::game
