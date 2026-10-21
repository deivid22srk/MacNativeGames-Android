// VP9 movie player for in-game movies — M4 scaffolding.
//
// Upstream decodes VP9 via libvpx (movie_host.c). libvpx is NOT vendored here
// yet (it is a CI FetchContent item planned for M4); until then this module
// reports its status explicitly instead of pretending to work.
#pragma once

#include <cstdint>
#include <string>

namespace mng::video {

struct PlayerStatus {
    bool libvpx_available = false; // false until libvpx is wired into CMake (M4)
    std::string note;
};

PlayerStatus player_status();

// Decodes nothing yet; exists so call sites and tests have a stable surface.
// Returns false while libvpx is unavailable.
bool decode_frame_stub(const uint8_t* vp9_frame, size_t size);

} // namespace mng::video
