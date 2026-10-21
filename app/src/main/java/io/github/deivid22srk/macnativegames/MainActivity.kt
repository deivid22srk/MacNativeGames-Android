package io.github.deivid22srk.macnativegames

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import io.github.deivid22srk.macnativegames.pipeline.BuildPipeline
import kotlin.concurrent.thread

/**
 * Tela principal (M0): importa prod.keys e ROM via SAF, valida estrutura e mostra o log.
 * Nenhum dado do jogo sai do aparelho; chaves nunca são exibidas.
 */
class MainActivity : Activity() {

    private companion object {
        const val REQ_KEYS = 11
        const val REQ_ROM = 12
    }

    private lateinit var pipeline: BuildPipeline
    private lateinit var statusView: TextView
    private lateinit var logView: TextView
    private lateinit var progress: ProgressBar
    private lateinit var btnRun: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        pipeline = BuildPipeline(this)

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(16))
            setBackgroundColor(Color.rgb(18, 20, 26))
            fitsSystemWindows = true
        }

        val title = TextView(this).apply {
            text = "MacNativeGames — Android (arm64-v8a)"
            textSize = 20f
            setTextColor(Color.WHITE)
            typeface = Typeface.DEFAULT_BOLD
        }
        val subtitle = TextView(this).apply {
            text = "Port independente, sem afiliação com a Nintendo. Sem conteúdo de jogo neste app: " +
                "você fornece seus próprios arquivos e todo o processamento é local."
            textSize = 12f
            setTextColor(Color.rgb(170, 178, 194))
            setPadding(0, dp(6), 0, dp(10))
        }

        fun mkButton(label: String, onClick: (View) -> Unit): Button =
            Button(this).apply { text = label; setOnClickListener(onClick) }

        val btnKeys = mkButton("1. Importar prod.keys") { pick(REQ_KEYS, "*/*") }
        val btnRom = mkButton("2. Importar ROM (NSP/XCI) ou NSO") { pick(REQ_ROM, "*/*") }
        btnRun = mkButton("3. Validar estrutura (M0)") { runValidation() }
        val btnVulkan = mkButton("4. Diagnóstico Vulkan (triângulo, M1)") {
            startActivity(Intent(this, GameActivity::class.java))
        }

        statusView = TextView(this).apply {
            text = "Status: aguardando arquivos."
            setTextColor(Color.rgb(120, 220, 180))
            setPadding(0, dp(8), 0, 0)
        }
        progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            max = 1000
        }
        logView = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 11f
            setTextColor(Color.rgb(200, 206, 220))
            setPadding(dp(8), dp(8), dp(8), dp(8))
        }
        val logScroll = ScrollView(this).apply {
            addView(logView)
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f
            ).setMargins(0, dp(10), 0, dp(6))
        }

        root.addView(title)
        root.addView(subtitle)
        root.addView(btnKeys)
        root.addView(btnRom)
        root.addView(btnRun)
        root.addView(btnVulkan)
        root.addView(progress)
        root.addView(statusView)
        root.addView(logScroll)
        setContentView(root)

        log("Pipeline M0: validação estrutural sem descriptografia (extração real no M2).")
    }

    private fun pick(requestCode: Int, mime: String) {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = mime
        }
        startActivityForResult(intent, requestCode)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if (resultCode != RESULT_OK || uri == null) return
        val label = when (requestCode) {
            REQ_KEYS -> "keys"
            REQ_ROM -> "rom"
            else -> return
        }
        toast("Copiando para o armazenamento privado do app…")
        thread {
            try {
                val f = pipeline.importFromUri(uri, label)
                if (label == "keys") pipeline.keysFile = f else pipeline.romFile = f
                ui {
                    statusView.text = "Importado: ${f.name} (${f.length()} bytes)"
                    log("Importado ($label): ${f.name}")
                }
            } catch (e: Exception) {
                ui {
                    statusView.text = "Falha na importação: ${e.message}"
                    log("ERRO: ${e.message}")
                }
            }
        }
    }

    private fun runValidation() {
        btnRun.isEnabled = false
        progress.progress = 0
        statusView.text = "Validando…"
        pipeline.setProgressCallback { f -> ui { progress.progress = (f * 1000).toInt() } }
        thread {
            pipeline.runM0(object : BuildPipeline.Listener {
                override fun onLog(message: String) = ui { log(message) }
                override fun onProgress(fraction: Float) = ui {
                    progress.progress = (fraction * 1000).toInt()
                }
                override fun onFinished(success: Boolean, summary: String) = ui {
                    btnRun.isEnabled = true
                    statusView.text = summary
                    statusView.setTextColor(
                        if (success) Color.rgb(120, 220, 180) else Color.rgb(240, 130, 130)
                    )
                    log(if (success) "OK: $summary" else "FALHA: $summary")
                }
            })
        }
    }

    private fun log(message: String) {
        logView.append("> $message\n")
        (logView.parent as? ScrollView)?.fullScroll(ScrollView.FOCUS_DOWN)
    }

    private fun toast(msg: String) =
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()

    private fun ui(block: () -> Unit) = runOnUiThread(block)

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()
}
