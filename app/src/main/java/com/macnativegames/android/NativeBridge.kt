package com.macnativegames.android

/**
 * JNI bridge to the native layer (graphics + NVN shim + runtime host).
 * The native side looks these methods up by name (see mng_jni.cpp).
 */
object NativeBridge {
    init {
        System.loadLibrary("mng_android")
    }

    /** M1: create Vulkan instance/swapchain for the given Android Surface. Returns 0 on success. */
    external fun vulkanInit(surface: Any): Int

    /** M1: render one frame. Returns number of frames rendered so far. */
    external fun vulkanRenderFrame(): Int

    /** M1: recreate swapchain after surface resize. Returns 0 on success. */
    external fun vulkanResize(width: Int, height: Int): Int

    /** M1: tear down swapchain/device/instance. */
    external fun vulkanShutdown()

    /** M2 scaffolding: report of the in-process Horizon host service registry (diagnostics only). */
    external fun hostRegistryReport(): String

    /** M3: draw the test triangle through the NVN→Vulkan shim registry. Returns a report. */
    external fun nvnProbe(surface: Any, frames: Int): String

    /** M4: start the sine tone probe through the mixer + AAudio. Returns 0 on success. */
    external fun audioToneStart(freqHz: Double): Int

    /** M4: stop the tone probe. */
    external fun audioToneStop()

    /** M4: AAudio diagnostics (open/rate/burst/xruns). */
    external fun audioStatus(): String

    /** M4: forward touch state (Android MotionEvent action + coordinates). */
    external fun inputTouch(action: Int, x: Float, y: Float)

    /** M4: forward gamepad key (Android keycode + action). Returns true when mapped. */
    external fun inputKey(keycode: Int, action: Int): Boolean

    /** M4: forward left-stick values normalized to [-1, 1]. */
    external fun inputStick(x: Float, y: Float)

    /** M4: human-readable input snapshot. */
    external fun inputStateReport(): String

    /** M5: diagnostics ring + device memory budget report. */
    external fun diagReport(): String
}
