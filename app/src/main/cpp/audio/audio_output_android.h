// M4 — AAudio output with a mixer-driven callback.
//
// The stream always drains through the mixer path: the callback pulls N frames
// from the active source (tone probe for M4 tests; the game's mixed output
// later). Silence when no source is active — the stream proves the device
// output works end to end with the 440 Hz probe tone.
#pragma once

#include <cstdint>

namespace mng::audio {

// Opens the output stream (stereo s16). Returns 0 on success.
int aaudio_open_output(int32_t sample_rate, int32_t channels);

// M4 probe: start/stop a sine tone routed through mix_stereo_s16.
int aaudio_tone_start(double freq_hz);
void aaudio_tone_stop();

// Diagnostics: current sample rate / frames per burst / underrun count.
struct OutputStatus {
    bool open = false;
    int32_t sample_rate = 0;
    int32_t frames_per_burst = 0;
    int32_t xruns = 0;
};
OutputStatus aaudio_status();

void aaudio_close_output();

} // namespace mng::audio
