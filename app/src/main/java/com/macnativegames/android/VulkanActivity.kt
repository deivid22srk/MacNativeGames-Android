package com.macnativegames.android

import android.app.Activity
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.FrameLayout
import android.widget.TextView
import java.util.concurrent.atomic.AtomicBoolean

/**
 * M1 — proves the graphics stack: a Vulkan instance, device, swapchain bound to this
 * Activity's SurfaceView and a render loop drawing the M1 test triangle (small
 * centered RGB-gradient triangle on a dark background).
 * This is NOT game rendering; it is the M1 acceptance probe.
 *
 * Concurrency contract: ALL native Vulkan calls happen on the single render
 * thread. surfaceChanged only raises a flag the render loop consumes — calling
 * native resize from the UI thread raced with in-flight submits.
 */
class VulkanActivity : Activity(), SurfaceHolder.Callback2 {

    private var surfaceReady = false
    private var renderThread: Thread? = null
    private lateinit var fpsText: TextView
    private val resizeRequested = AtomicBoolean(false)

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
            runOnUiThread {
                fpsText.text = "M1 Vulkan triangle — initializing"
            }
            var last = System.nanoTime()
            var frames = 0
            while (surfaceReady && !Thread.currentThread().isInterrupted) {
                if (resizeRequested.compareAndSet(true, false)) {
                    NativeBridge.vulkanResize(0, 0) // native re-queries surface caps
                }
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
        // Consume on the render thread; never touch Vulkan from the UI thread.
        if (surfaceReady) resizeRequested.set(true)
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
