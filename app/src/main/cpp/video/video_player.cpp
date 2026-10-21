#include "video/video_player.h"

namespace mng::video {

PlayerStatus player_status() {
    return PlayerStatus{
        false,
        "libvpx not wired into the Android CMake build yet (planned M4, via "
        "CMake FetchContent in CI). Movie playback is honestly unavailable.",
    };
}

bool decode_frame_stub(const uint8_t*, size_t) {
    return false;
}

} // namespace mng::video
