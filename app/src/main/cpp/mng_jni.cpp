// JNI bridge between the Kotlin app and the native port layer.
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <sstream>

#include "diag/diag.h"
#include "gfx/vulkan_renderer.h"
#include "input/input_manager.h"
#include "nvn/nvn_shim.h"
#include "nvn/nvn_types.h"
#include "runtime/horizon_host.h"
#include "video/video_player.h"
#include "audio/audio_output_android.h"

#define LOG_TAG "mng-jni"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

mng::gfx::VulkanRenderer g_renderer;
mng::horizon::ServiceRegistry g_host;

// The Horizon host registry is built once; M2 wires real transports to it.
bool ensure_host_registry() {
    static bool ready = false;
    if (ready) return true;
    bool ok = g_host.register_service("time:u", mng::horizon::make_time_service());
    ok = g_host.register_service("set:sys", mng::horizon::make_settings_service()) && ok;
    ready = ok;
    return ok;
}

} // namespace

extern "C" {

// ---- M1 ---------------------------------------------------------------------

JNIEXPORT jint JNICALL
Java_com_macnativegames_android_NativeBridge_vulkanInit(JNIEnv* env, jobject /*thiz*/,
                                                        jobject surface) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) return -1;
    const int rc = g_renderer.init(window);
    ANativeWindow_release(window);
    if (rc != 0) LOGI("vulkanInit failed rc=%d", rc);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_macnativegames_android_NativeBridge_vulkanRenderFrame(JNIEnv* /*env*/,
                                                               jobject /*thiz*/) {
    return g_renderer.render_frame();
}

JNIEXPORT jint JNICALL
Java_com_macnativegames_android_NativeBridge_vulkanResize(JNIEnv* /*env*/, jobject /*thiz*/,
                                                          jint width, jint height) {
    return g_renderer.resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

JNIEXPORT void JNICALL
Java_com_macnativegames_android_NativeBridge_vulkanShutdown(JNIEnv* /*env*/, jobject /*thiz*/) {
    g_renderer.shutdown();
}

// ---- M0 ---------------------------------------------------------------------

JNIEXPORT jstring JNICALL
Java_com_macnativegames_android_NativeBridge_hostRegistryReport(JNIEnv* env, jobject /*thiz*/) {
    std::ostringstream os;
    if (ensure_host_registry()) {
        os << "ok; " << g_host.size() << " service(s):";
        for (const auto& n : g_host.names()) os << ' ' << n;
    } else {
        os << "registry bootstrap failed";
    }
    const auto status = mng::video::player_status();
    os << "; video: " << (status.libvpx_available ? "libvpx ready" : status.note);
    return env->NewStringUTF(os.str().c_str());
}

// ---- M3 ---------------------------------------------------------------------

// Runs the M3 acceptance probe on the given surface: resolves NVN entry points
// BY NAME from the shim registry and renders the test triangle through them.
// Returns a sanitized report line set for the probe screen.
JNIEXPORT jstring JNICALL
Java_com_macnativegames_android_NativeBridge_nvnProbe(JNIEnv* env, jobject /*thiz*/,
                                                      jobject surface, jint frames) {
    mng::diag::event("m3", "probe requested");
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) return env->NewStringUTF("M3 probe FAILED: null surface");
    const int rc = mng::nvn::probe_render_triangle(window, frames);
    ANativeWindow_release(window);

    std::ostringstream os;
    const auto& c = mng::nvn::counters();
    os << "M3 NVN probe: " << (rc == 0 ? "OK" : "FAILED") << " (rc=" << rc << ")\n";
    os << "entry points registered: " << mng::nvn::shim_registered_entries() << "\n";
    os << "device init ok/fail: " << c.device_initialize_ok << "/" << c.device_initialize_fail
       << "\n";
    os << "queue submits ok: " << c.queue_submit_ok << "\n";
    os << "present ok/fail: " << c.present_ok << "/" << c.present_fail << "\n";
    os << "shaders registered: " << mng::nvn::shim_shader_count()
       << "; pipelines: " << c.pipeline_ok << "\n";
    os << "ops replayed: " << c.command_buffer_records
       << "; skipped (not implemented): " << c.command_buffer_unimplemented_calls << "\n";
    if (rc != 0) {
        mng::diag::event("m3", "probe failed rc=" + std::to_string(rc));
    }
    return env->NewStringUTF(os.str().c_str());
}

// ---- M4 ---------------------------------------------------------------------

JNIEXPORT jint JNICALL
Java_com_macnativegames_android_NativeBridge_audioToneStart(JNIEnv* /*env*/,
                                                            jobject /*thiz*/,
                                                            jdouble freq) {
    mng::diag::event("m4", "tone probe start");
    return mng::audio::aaudio_tone_start(freq);
}

JNIEXPORT void JNICALL
Java_com_macnativegames_android_NativeBridge_audioToneStop(JNIEnv* /*env*/,
                                                           jobject /*thiz*/) {
    mng::audio::aaudio_tone_stop();
}

JNIEXPORT jstring JNICALL
Java_com_macnativegames_android_NativeBridge_audioStatus(JNIEnv* env, jobject /*thiz*/) {
    const auto st = mng::audio::aaudio_status();
    std::ostringstream os;
    os << (st.open ? "open" : "closed") << "; rate=" << st.sample_rate
       << "; burst=" << st.frames_per_burst << "; xruns=" << st.xruns;
    return env->NewStringUTF(os.str().c_str());
}

JNIEXPORT void JNICALL
Java_com_macnativegames_android_NativeBridge_inputTouch(JNIEnv* /*env*/, jobject /*thiz*/,
                                                        jint action, jfloat x, jfloat y) {
    mng::input::on_touch(action, x, y);
}

JNIEXPORT jboolean JNICALL
Java_com_macnativegames_android_NativeBridge_inputKey(JNIEnv* /*env*/, jobject /*thiz*/,
                                                      jint keycode, jint action) {
    return mng::input::on_key(keycode, action) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_macnativegames_android_NativeBridge_inputStick(JNIEnv* /*env*/, jobject /*thiz*/,
                                                        jfloat x, jfloat y) {
    mng::input::on_stick(x, y);
}

JNIEXPORT jstring JNICALL
Java_com_macnativegames_android_NativeBridge_inputStateReport(JNIEnv* env,
                                                              jobject /*thiz*/) {
    const auto st = mng::input::snapshot();
    std::ostringstream os;
    os << "touch: " << (st.touch_down ? "down" : "up") << " (" << st.touch_x << ","
       << st.touch_y << "); buttons=0x" << std::hex << st.buttons << std::dec
       << "; stick=(" << st.stick_lx << "," << st.stick_ly << ")";
    return env->NewStringUTF(os.str().c_str());
}

// ---- M5 ---------------------------------------------------------------------

JNIEXPORT jstring JNICALL
Java_com_macnativegames_android_NativeBridge_diagReport(JNIEnv* env, jobject /*thiz*/) {
    std::ostringstream os;
    const auto b = mng::diag::budget_from_device();
    os << "memory: device=" << b.device_mem_total_mib << " MiB; game budget="
       << b.budget_mib << " MiB (addressable " << b.game_addressable_mib << " MiB)\n";
    os << "diagnostics ring:\n" << mng::diag::dump();
    return env->NewStringUTF(os.str().c_str());
}

} // extern "C"
