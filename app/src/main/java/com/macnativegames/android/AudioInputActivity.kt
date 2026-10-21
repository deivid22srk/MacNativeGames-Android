package com.macnativegames.android

import android.app.Activity
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/**
 * M4 — audio + input probes:
 *  * "Tone" plays a 440 Hz sine routed through the mixer into AAudio (proves
 *    the device audio path end to end; stop silences it).
 *  * The dark area below forwards touches into the native input state; gamepad
 *    buttons (A/B/X/Y/L/R/ZL/ZR/Start/Select/D-pad) map to the Switch layout.
 * The status line reports the AAudio + input state.
 */
class AudioInputActivity : Activity() {

    private lateinit var statusText: TextView
    private var toneOn = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = (16 * resources.displayMetrics.density).toInt()

        statusText = TextView(this).apply { setPadding(pad, pad, pad, pad) }

        val toneBtn = Button(this).apply {
            text = getString(R.string.btn_tone)
            setOnClickListener {
                toneOn = !toneOn
                if (toneOn) NativeBridge.audioToneStart(440.0) else NativeBridge.audioToneStop()
                text = if (toneOn) getString(R.string.btn_tone_stop) else getString(R.string.btn_tone)
                refresh()
            }
        }

        val touchArea = TextView(this).apply {
            text = getString(R.string.touch_area_hint)
            setBackgroundColor(0xFF181C22.toInt())
            setPadding(pad, pad * 2, pad, pad * 2)
            setOnTouchListener { v, e ->
                NativeBridge.inputTouch(e.actionMasked, e.x, e.y)
                refresh()
                v.performClick()
                true
            }
        }

        val root = ScrollView(this).apply {
            addView(LinearLayout(this@AudioInputActivity).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(pad, pad, pad, pad)
                addView(statusText)
                addView(toneBtn)
                addView(touchArea)
            })
        }
        setContentView(root)
        refresh()
    }

    override fun onDestroy() {
        super.onDestroy()
        if (toneOn) NativeBridge.audioToneStop()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        if (NativeBridge.inputKey(keyCode, 0)) { refresh(); return true }
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent?): Boolean {
        if (NativeBridge.inputKey(keyCode, 1)) { refresh(); return true }
        return super.onKeyUp(keyCode, event)
    }

    override fun onGenericMotionEvent(event: MotionEvent?): Boolean {
        if (event != null && event.source and android.view.InputDevice.SOURCE_JOYSTICK != 0) {
            // Normalize axis values to [-1, 1] before handing them to the shim.
            val x = event.getAxisValue(MotionEvent.AXIS_X)
            val y = -event.getAxisValue(MotionEvent.AXIS_Y) // Android Y grows downward
            NativeBridge.inputStick(x, y)
            refresh()
            return true
        }
        return super.onGenericMotionEvent(event)
    }

    private fun refresh() {
        statusText.text = "AAudio: " + NativeBridge.audioStatus() +
            "\ninput: " + NativeBridge.inputStateReport()
    }
}
