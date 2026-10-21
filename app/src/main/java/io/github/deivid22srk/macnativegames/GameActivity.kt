package io.github.deivid22srk.macnativegames

import android.app.Activity
import android.os.Bundle
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.FrameLayout

/**
 * Diagnóstico Vulkan (marco M1): SurfaceView + renderer nativo (triângulo).
 * O runtime completo do jogo NÃO roda aqui — é apenas o caminho gráfico mínimo.
 */
class GameActivity : Activity(), SurfaceHolder.Callback2 {

    private companion object {
        init {
            System.loadLibrary("macnative")
        }
    }

    private lateinit var surfaceView: SurfaceView
    private lateinit var controls: ui.TouchControlsView
    private var surfaceAlive = false

    private external fun nativeStartRenderer(surface: Surface): Boolean
    private external fun nativeStopRenderer()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = FrameLayout(this)
        surfaceView = SurfaceView(this).apply {
            holder.addCallback(this@GameActivity)
        }
        controls = ui.TouchControlsView(this)
        root.addView(
            surfaceView, FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT
            )
        )
        root.addView(
            controls, FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT
            )
        )
        setContentView(root)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceAlive = true
        val s: Surface = holder.surface
        thread {
            val ok = nativeStartRenderer(s)
            if (!ok) runOnUiThread { controls.setStatus("Falha ao iniciar o renderer Vulkan") }
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        // O renderer consulta as dimensões do ANativeWindow a cada frame (recria
        // o swapchain quando mudam), então nenhum callback nativo é necessário aqui.
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceAlive = false
        nativeStopRenderer()
    }

    override fun surfaceRedrawNeeded(holder: SurfaceHolder) = Unit

    override fun onDestroy() {
        if (surfaceAlive) {
            nativeStopRenderer()
            surfaceAlive = false
        }
        super.onDestroy()
    }
}
