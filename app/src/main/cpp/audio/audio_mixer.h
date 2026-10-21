// Audio mixer math for the Android port — M4 scaffolding.
//
// Ported approach mirrors upstream games/totk/runtime/audio_mixer.c (itself a
// port of Ryujinx.Audio): stereo interleaved s16 streams, per-stream gain,
// simple clamp mixing. The math is pure C++ so the host test suite can verify
// it without an audio device. The device output path (AAudio) lives in
// audio_output_android.cpp and is marked NOT TESTED ON DEVICE in the status doc.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mng::audio {

// Mixes `inputs` stereo s16 interleaved streams (L,R,L,R...) into `out`.
// All streams must be the same length. Gain is applied per stream before
// summation; result clamps to int16 range. `out` must be sized >= frames*2.
bool mix_stereo_s16(const std::vector<const int16_t*>& inputs,
                    size_t frames,
                    const float* gains, // one per input stream
                    int16_t* out);

// Applies a linear volume ramp over `frames` frames (per sample pair), useful
// against audible clicks when streams start/stop.
void volume_ramp_s16(int16_t* samples, size_t frames, float from, float to);

} // namespace mng::audio
