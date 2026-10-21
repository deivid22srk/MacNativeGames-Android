package com.macnativegames.android

/**
 * JNI bridge to the native layer (graphics + future runtime host).
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
}
