package com.macnativegames.android

import android.app.Activity
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.FrameLayout
import android.widget.TextView
import java.util.concurrent.atomic.AtomicBoolean

/**
 * M3 — NVN→Vulkan shim acceptance probe. Draws the test triangle by resolving
 * the NVN entry points BY NAME from the shim registry (nvnBootstrapLoader /
 * nvnDeviceGetProcAddress path), exactly like the guest will. Shows the shim
 * counters afterwards. No game data involved.
 */
class NvnProbeActivity : Activity(), SurfaceHolder.Callback2 {

    private lateinit var statusText: TextView
    private val ran = AtomicBoolean(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val frame = FrameLayout(this)
        val surface = SurfaceView(this)
        surface.holder.addCallback(this)
        statusText = TextView(this).apply { setPadding(32f.dp(), 32f.dp(), 32f.dp(), 0f.dp()) }
        frame.addView(surface, FrameLayout.LayoutParams(-1, -1))
        frame.addView(statusText, FrameLayout.LayoutParams(-2, -2))
        setContentView(frame)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (!ran.compareAndSet(false, true)) return
        Thread {
            val sf = holder.surface
            val report = NativeBridge.nvnProbe(sf, 120)
            runOnUiThread {
                statusText.text = report
            }
        }.start()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit
    override fun surfaceDestroyed(holder: SurfaceHolder) = Unit
    override fun surfaceRedrawNeeded(holder: SurfaceHolder) = Unit

    private fun Float.dp(): Int = (this * resources.displayMetrics.density).toInt()
}
