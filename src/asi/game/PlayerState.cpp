#include "PlayerState.h"
#include <mutex>

namespace snowmap::game {
namespace {

std::mutex     g_mu;
PlayerSnapshot g_snap;
uint64_t       g_stamp     = 0;
bool           g_hasCamera = false;

} // namespace

PlayerSnapshot PlayerState::Current() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_snap;
}

void PlayerState::UpdateCamera(float x, float y, float z, float hr) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_snap.eye_world[0] = x;
    g_snap.eye_world[1] = y;
    g_snap.eye_world[2] = z;
    g_snap.heading_rad  = hr;
    g_hasCamera         = true;
    g_snap.valid        = true;
    g_snap.stamp        = ++g_stamp;
}

void PlayerState::ResetOffsets() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_hasCamera  = false;
    g_snap.valid = false;
}

} // namespace snowmap::game
