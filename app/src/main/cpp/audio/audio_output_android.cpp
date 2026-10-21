#include "audio/audio_output_android.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <cmath>
#include <cstring>
#include <mutex>

#include "audio/audio_mixer.h"

namespace mng::audio {
namespace {

constexpr const char* kTag = "mng-audio";

AAudioStream* g_stream = nullptr;
int32_t g_rate = 0;
int32_t g_burst = 0;
int32_t g_xruns = 0;

// Tone probe state (touched only from the AAudio callback + start/stop with
// the stream stopped, so a plain mutex is enough).
std::mutex g_tone_mu;
bool g_tone_on = false;
double g_tone_freq = 440.0;
double g_tone_phase = 0.0;

aaudio_data_callback_result_t callback(AAudioStream* stream, void* /*user_data*/,
                                       void* audio_data, int32_t num_frames) {
    auto* out = static_cast<int16_t*>(audio_data);
    {
        std::lock_guard<std::mutex> lk(g_tone_mu);
        if (!g_tone_on) {
            std::memset(out, 0, static_cast<size_t>(num_frames) * 2 * sizeof(int16_t));
            return AAUDIO_CALLBACK_RESULT_CONTINUE;
        }
        // Generate the sine in float, then mix it through the mixer path the
        // game will use — keeps one code path under test.
        static thread_local std::vector<int16_t> tone;
        tone.resize(static_cast<size_t>(num_frames) * 2);
        for (int32_t i = 0; i < num_frames; ++i) {
            const double s = std::sin(g_tone_phase);
            const auto v = static_cast<int16_t>(s * 12000.0);
            tone[2 * i] = v;
            tone[2 * i + 1] = v;
            g_tone_phase += 2.0 * M_PI * g_tone_freq / double(g_rate);
            if (g_tone_phase >= 2.0 * M_PI) g_tone_phase -= 2.0 * M_PI;
        }
        const std::vector<const int16_t*> inputs = {tone.data()};
        const float gain = 1.0f;
        if (!mix_stereo_s16(inputs, static_cast<size_t>(num_frames), &gain, out)) {
            std::memset(out, 0, static_cast<size_t>(num_frames) * 2 * sizeof(int16_t));
        }
    }
    const int32_t burst = AAudioStream_getFramesPerBurst(stream);
    (void)burst;
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void error_callback(AAudioStream*, void*, aaudio_result_t error) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "AAudio error: %s",
                        AAudio_convertResultToText(error));
}

} // namespace

int aaudio_open_output(int32_t sample_rate, int32_t channels) {
    std::lock_guard<std::mutex> lk(g_tone_mu);
    if (g_stream != nullptr) return 0; // already open
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return -1;
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, channels);
    AAudioStreamBuilder_setSampleRate(builder, sample_rate);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, callback, nullptr);
    AAudioStreamBuilder_setErrorCallback(builder, error_callback, nullptr);

    AAudioStream* stream = nullptr;
    const aaudio_result_t rc = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (rc != AAUDIO_OK) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "open failed: %s",
                            AAudio_convertResultToText(rc));
        return -2;
    }
    g_rate = AAudioStream_getSampleRate(stream);
    g_burst = AAudioStream_getFramesPerBurst(stream);
    AAudioStream_requestStart(stream);
    g_stream = stream;
    return 0;
}

int aaudio_tone_start(double freq_hz) {
    if (g_stream == nullptr) {
        if (aaudio_open_output(48000, 2) != 0) return -1;
    }
    std::lock_guard<std::mutex> lk(g_tone_mu);
    g_tone_freq = freq_hz;
    g_tone_phase = 0.0;
    g_tone_on = true;
    return 0;
}

void aaudio_tone_stop() {
    std::lock_guard<std::mutex> lk(g_tone_mu);
    g_tone_on = false;
}

OutputStatus aaudio_status() {
    std::lock_guard<std::mutex> lk(g_tone_mu);
    OutputStatus st;
    st.open = g_stream != nullptr;
    st.sample_rate = g_rate;
    st.frames_per_burst = g_burst;
    if (g_stream != nullptr) {
        const int32_t underruns = AAudioStream_getXRunCount(g_stream);
        if (underruns > 0) g_xruns = underruns;
    }
    st.xruns = g_xruns;
    return st;
}

void aaudio_close_output() {
    std::lock_guard<std::mutex> lk(g_tone_mu);
    if (g_stream == nullptr) return;
    AAudioStream_requestStop(g_stream);
    AAudioStream_close(g_stream);
    g_stream = nullptr;
}

} // namespace mng::audio
