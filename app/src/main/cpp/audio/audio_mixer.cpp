#include "audio/audio_mixer.h"

#include <cmath>

namespace mng::audio {

static inline int16_t clamp_i16(float v) {
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return static_cast<int16_t>(lrintf(v));
}

bool mix_stereo_s16(const std::vector<const int16_t*>& inputs,
                    size_t frames,
                    const float* gains,
                    int16_t* out) {
    if (out == nullptr) return false;
    for (const auto* in : inputs) {
        if (in == nullptr) return false;
    }
    std::vector<float> acc(frames * 2, 0.0f);
    for (size_t s = 0; s < inputs.size(); ++s) {
        const float g = gains[s];
        const int16_t* src = inputs[s];
        for (size_t i = 0; i < frames * 2; ++i) {
            acc[i] += static_cast<float>(src[i]) * g;
        }
    }
    for (size_t i = 0; i < frames * 2; ++i) {
        out[i] = clamp_i16(acc[i]);
    }
    return true;
}

void volume_ramp_s16(int16_t* samples, size_t frames, float from, float to) {
    if (frames == 0) return;
    const float step = (to - from) / static_cast<float>(frames);
    float g = from;
    for (size_t f = 0; f < frames; ++f) {
        for (int ch = 0; ch < 2; ++ch) {
            samples[f * 2 + ch] = clamp_i16(static_cast<float>(samples[f * 2 + ch]) * g);
        }
        g += step;
    }
}

} // namespace mng::audio
