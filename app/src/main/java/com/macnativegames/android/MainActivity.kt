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

        val root = ScrollView(this).apply {
            setPadding(pad, pad, pad, pad)
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.VERTICAL
                addView(statusText)
                addView(gameBtn)
                addView(keysBtn)
                addView(validateBtn)
                addView(playBtn)
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

    private fun validateLocal() {
        val g = gameUri
        if (g == null) { log("ERROR: pick a game file first"); return }
        try {
            contentResolver.openInputStream(g)?.use { input ->
                val size = (contentResolver.openFileDescriptor(g, "r")?.use { it.statSize }) ?: -1L
                val keysStream = keysUri?.let { k -> contentResolver.openInputStream(k) }
                val report = input.use { i ->
                    val name = g.lastPathSegment ?: "game"
                    GameFileValidator.validate(i, name, size, keysStream)
                }
                report.toSanitizedLines().forEach { log(it) }
            } ?: log("ERROR: cannot open the selected game file")
        } catch (t: Throwable) {
            log("ERROR: ${t.message}")
        }
    }

    private fun log(line: String) {
        logText.append("\n" + line)
    }

    companion object {
        private const val GAME_RQ = 11
        private const val KEYS_RQ = 12
    }
}
