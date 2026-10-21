#include "audio/audio_output_android.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <cstring>

namespace mng::audio {
namespace {

constexpr const char* kTag = "mng-audio";

AAudioStream* g_stream = nullptr;

aaudio_data_callback_result_t callback(AAudioStream* stream, void* user_data,
                                       void* audio_data, int32_t num_frames) {
    // M4: feed from the mixer ring buffer. Silence for now — the scaffolding
    // only proves the stream can open and drain.
    std::memset(audio_data, 0, static_cast<size_t>(num_frames) * 2 * sizeof(int16_t));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void error_callback(AAudioStream*, void*, aaudio_result_t error) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "AAudio error: %s", AAudio_convertResultToText(error));
}

} // namespace

void* aaudio_open_output(int32_t sample_rate, int32_t channels) {
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return nullptr;
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
        return nullptr;
    }
    AAudioStream_requestStart(stream);
    g_stream = stream;
    return stream;
}

bool aaudio_write(void* handle, const int16_t* frames, int32_t frame_count) {
    auto* stream = static_cast<AAudioStream*>(handle);
    if (stream == nullptr) return false;
    // With a data callback installed the stream drains via the callback; a
    // blocking write path is provided for the future non-callback mode (M4).
    constexpr int64_t kTimeoutNs = 100LL * 1000 * 1000; // 100 ms
    return AAudioStream_write(stream, frames, frame_count, kTimeoutNs) > 0;
}

void aaudio_close(void* handle) {
    auto* stream = static_cast<AAudioStream*>(handle);
    if (stream == nullptr) return;
    AAudioStream_requestStop(stream);
    AAudioStream_close(stream);
    if (g_stream == stream) g_stream = nullptr;
}

} // namespace mng::audio
