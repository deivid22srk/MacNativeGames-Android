package com.macnativegames.android

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/**
 * M0 — the app entry point. Pick user files with SAF, validate locally, show an
 * honest log. No INTERNET permission exists in the manifest: nothing leaves the device.
 */
class MainActivity : Activity() {

    private lateinit var statusText: TextView
    private lateinit var logText: TextView
    private var gameUri: Uri? = null
    private var keysUri: Uri? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = (16 * resources.displayMetrics.density).toInt()
        val pad8 = (8 * resources.displayMetrics.density).toInt()

        statusText = TextView(this).apply { text = getString(R.string.title_status); setPadding(pad8, 0, pad8, pad8) }
        logText = TextView(this).apply {
            setPadding(pad8, pad8, pad8, pad8)
            setTextIsSelectable(true)
            text = getString(R.string.validation_log) + ":\n"
        }

        val gameBtn = Button(this).apply {
            text = getString(R.string.pick_game)
            setOnClickListener { openPicker(GAME_RQ) }
        }
        val keysBtn = Button(this).apply {
            text = getString(R.string.pick_keys)
            setOnClickListener { openPicker(KEYS_RQ) }
        }
        val validateBtn = Button(this).apply {
            text = getString(R.string.btn_validate)
            setOnClickListener { validateLocal() }
        }
        val playBtn = Button(this).apply {
            text = getString(R.string.btn_play)
            setOnClickListener { startActivity(android.content.Intent(this@MainActivity, VulkanActivity::class.java)) }
        }
        val m3Btn = Button(this).apply {
            text = getString(R.string.btn_m3)
            setOnClickListener { startActivity(android.content.Intent(this@MainActivity, NvnProbeActivity::class.java)) }
        }
        val m4Btn = Button(this).apply {
            text = getString(R.string.btn_m4)
            setOnClickListener { startActivity(android.content.Intent(this@MainActivity, AudioInputActivity::class.java)) }
        }
        val m5Btn = Button(this).apply {
            text = getString(R.string.btn_m5)
            setOnClickListener { startActivity(android.content.Intent(this@MainActivity, DiagActivity::class.java)) }
        }

        val root = ScrollView(this).apply {
            setPadding(pad, pad, pad, pad)
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.VERTICAL
                addView(statusText)
                addView(gameBtn)
                addView(keysBtn)
                addView(validateBtn)
                addView(playBtn)
                addView(m3Btn)
                addView(m4Btn)
                addView(m5Btn)
                addView(logText)
            })
        }
        setContentView(root)
        log("M0 shell ready. Files stay on this device; nothing is uploaded.")
        log("Native host registry: " + runCatching { NativeBridge.hostRegistryReport() }.getOrElse { "native lib error: ${it.message}" })
    }

    private fun openPicker(requestCode: Int) {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
        }
        startActivityForResult(intent, requestCode)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        when (requestCode) {
            GAME_RQ -> { gameUri = uri; log("game file selected: ${uri.lastPathSegment ?: uri}") }
            KEYS_RQ -> { keysUri = uri; log("keys file selected: ${uri.lastPathSegment ?: uri}") }
        }
    }

    // Validation streams SHA-256 over potentially multi-GB files: it MUST run
    // off the UI thread or Android kills the app with an ANR (signal 3).
    private val validating = java.util.concurrent.atomic.AtomicBoolean(false)

    private fun validateLocal() {
        val g = gameUri
        if (g == null) { log("ERROR: pick a game file first"); return }
        if (!validating.compareAndSet(false, true)) {
            log("validation already running — wait for it to finish"); return
        }
        val k = keysUri
        log("validation started (background thread) — UI stays responsive")
        Thread {
            var report: ValidationReport? = null
            var failure: String? = null
            try {
                val size = contentResolver.openFileDescriptor(g, "r")?.use { it.statSize } ?: -1L
                val name = g.lastPathSegment ?: "game"
                contentResolver.openInputStream(g)?.use { input ->
                    val keysStream = k?.let { kk -> contentResolver.openInputStream(kk) }
                    keysStream?.use { keys ->
                        report = GameFileValidator.validate(input, name, size, keys)
                    } ?: run { report = GameFileValidator.validate(input, name, size, null) }
                } ?: run { failure = "cannot open the selected game file" }
            } catch (t: Throwable) {
                failure = t.message ?: t.javaClass.simpleName
            } finally {
                validating.set(false)
            }
            val finishedReport = report
            val finishedFailure = failure
            runOnUiThread {
                when {
                    finishedFailure != null -> log("ERROR: $finishedFailure")
                    finishedReport != null -> finishedReport.toSanitizedLines().forEach { line ->
                        log(line)
                    }
                    else -> log("ERROR: cannot open the selected game file")
                }
                log("validation finished — UI thread was never blocked")
            }
        }.start()
    }

    private fun log(line: String) {
        logText.append("\n" + line)
    }

    companion object {
        private const val GAME_RQ = 11
        private const val KEYS_RQ = 12
    }
}
