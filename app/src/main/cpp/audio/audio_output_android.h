// AAudio output path — M4 scaffolding. Compiles on the NDK; there is no audio
// device in CI, so this path is honest about being untested until a device run.
#pragma once

#include <cstdint>

namespace mng::audio {

// Opens a stereo s16 48kHz AAudio stream in low-latency mode.
// Returns an opaque handle or nullptr (device-only; CI never calls this).
void* aaudio_open_output(int32_t sample_rate = 48000, int32_t channels = 2);

// Writes interleaved frames to the opened stream (blocking).
bool aaudio_write(void* handle, const int16_t* frames, int32_t frame_count);

void aaudio_close(void* handle);

} // namespace mng::audio
