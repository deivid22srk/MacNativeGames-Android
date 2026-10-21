package com.macnativegames.android

import android.app.Activity
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.FrameLayout
import android.widget.TextView

/**
 * M1 — proves the graphics stack: a Vulkan instance, device, swapchain bound to this
 * Activity's SurfaceView and a render loop drawing the M1 test triangle.
 * This is NOT game rendering; it is the M1 acceptance probe.
 */
class VulkanActivity : Activity(), SurfaceHolder.Callback2 {

    private var surfaceReady = false
    private var renderThread: Thread? = null
    private lateinit var fpsText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val frame = FrameLayout(this)
        val surface = SurfaceView(this)
        surface.holder.addCallback(this)
        fpsText = TextView(this).apply { setPadding(32f.dp(), 32f.dp(), 32f.dp(), 0f.dp()) }
        frame.addView(surface, FrameLayout.LayoutParams(-1, -1))
        frame.addView(fpsText, FrameLayout.LayoutParams(-2, -2))
        setContentView(frame)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceReady = true
        renderThread = Thread {
            val sf = holder.surface
            val rc = NativeBridge.vulkanInit(sf)
            if (rc != 0) {
                runOnUiThread { fpsText.text = "Vulkan init failed (rc=$rc)" }
                return@Thread
            }
            var last = System.nanoTime()
            var frames = 0
            while (surfaceReady && !Thread.currentThread().isInterrupted) {
                val n = NativeBridge.vulkanRenderFrame()
                frames++
                val now = System.nanoTime()
                if (now - last >= 500_000_000L) {
                    val fps = frames * 1_000_000_000L / (now - last)
                    runOnUiThread { fpsText.text = "M1 Vulkan triangle — $fps fps (frame $n)" }
                    last = now
                    frames = 0
                }
            }
            NativeBridge.vulkanShutdown()
        }.apply { start() }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        if (surfaceReady) NativeBridge.vulkanResize(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false
        renderThread?.interrupt()
        renderThread?.join(2000)
        renderThread = null
    }

    // Required by Callback2; unused without hand-managed display refresh.
    override fun surfaceRedrawNeeded(holder: SurfaceHolder) = Unit

    private fun Float.dp(): Int = (this * resources.displayMetrics.density).toInt()
}
