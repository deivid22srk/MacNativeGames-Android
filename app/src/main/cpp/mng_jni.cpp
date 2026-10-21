// JNI bridge between the Kotlin app and the native port layer.
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <sstream>

#include "gfx/vulkan_renderer.h"
#include "runtime/horizon_host.h"
#include "video/video_player.h"

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

} // extern "C"
