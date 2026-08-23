#include "Waypoints.h"

#include <mutex>

namespace snowmap::game {
namespace {

std::mutex            g_mutex;
std::vector<Waypoint> g_waypoints;

} // namespace

void Waypoints::Add(float x, float y, float z)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Waypoint point;
    point.world[0] = x;
    point.world[1] = y;
    point.world[2] = z;
    g_waypoints.push_back(point);
}

void Waypoints::Clear()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_waypoints.clear();
}

void Waypoints::OnLevelChanged() { Clear(); }

std::vector<Waypoint> Waypoints::All()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_waypoints;
}

size_t Waypoints::Count()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_waypoints.size();
}

} // namespace snowmap::game
