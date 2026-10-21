package com.macnativegames.android

import android.app.Activity
import android.os.Bundle
import android.view.View
import android.widget.ScrollView
import android.widget.TextView

/** M5 — diagnostics screen: memory budget + event ring dump. */
class DiagActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = (16 * resources.displayMetrics.density).toInt()
        val text = TextView(this).apply {
            setPadding(pad, pad, pad, pad)
            setTextIsSelectable(true)
            text = NativeBridge.diagReport()
        }
        setContentView(ScrollView(this).apply { addView(text) })
    }
}
