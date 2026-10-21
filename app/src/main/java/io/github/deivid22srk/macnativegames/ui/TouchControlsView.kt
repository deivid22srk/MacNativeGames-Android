package io.github.deivid22srk.macnativegames.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import android.util.Log

/**
 * Overlay de controles por toque no estilo Switch (referência: touch_pad.m do projeto
 * original / iOS). Marco M4 completo (hid/host integrado) — aqui apenas captura e desenho,
 * com eventos registrados no log para diagnóstico.
 */
class TouchControlsView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {

    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 3f
        color = Color.argb(180, 255, 255, 255)
    }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = Color.argb(40, 255, 255, 255)
    }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(220, 255, 255, 255)
        textSize = 34f
        textAlign = Paint.Align.CENTER
    }
    private val statusPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(220, 140, 230, 200)
        textSize = 28f
        textAlign = Paint.Align.LEFT
    }

    private var statusText = "Vulkan M1: aguardando quadros…"

    private data class Stick(var cx: Float = 0f, var cy: Float = 0f, var r: Float = 0f, var active: Boolean = false)
    private val leftStick = Stick()
    private val rightStick = Stick()
    private val buttons = mutableListOf<Triple<String, Float, Float>>() // label, x, y

    fun setStatus(s: String) {
        statusText = s
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        val r = h * 0.11f
        leftStick.cx = w * 0.14f; leftStick.cy = h * 0.72f; leftStick.r = r
        rightStick.cx = w * 0.86f; rightStick.cy = h * 0.72f; rightStick.r = r
        buttons.clear()
        val bx = w * 0.86f
        val by = h * 0.42f
        val d = h * 0.09f
        buttons.add(Triple("X", bx, by - d))
        buttons.add(Triple("B", bx, by + d))
        buttons.add(Triple("Y", bx - d, by))
        buttons.add(Triple("A", bx + d, by))
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawCircle(leftStick.cx, leftStick.cy, leftStick.r, fill)
        canvas.drawCircle(leftStick.cx, leftStick.cy, leftStick.r, stroke)
        canvas.drawCircle(rightStick.cx, rightStick.cy, rightStick.r, fill)
        canvas.drawCircle(rightStick.cx, rightStick.cy, rightStick.r, stroke)
        for ((label, x, y) in buttons) {
            canvas.drawCircle(x, y, 34f, fill)
            canvas.drawCircle(x, y, 34f, stroke)
            canvas.drawText(label, x, y + 12f, text)
        }
        canvas.drawText(statusText, 24f, height - 40f, statusPaint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                updateStick(event.getX(i), event.getY(i), true)
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) updateStick(event.getX(i), event.getY(i), true)
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP,
            MotionEvent.ACTION_CANCEL -> {
                val i = event.actionIndex
                updateStick(event.getX(i), event.getY(i), false)
            }
        }
        return true
    }

    private fun updateStick(x: Float, y: Float, down: Boolean) {
        val left = dist(x, y, leftStick.cx, leftStick.cy) < leftStick.r * 2.2f
        val right = dist(x, y, rightStick.cx, rightStick.cy) < rightStick.r * 2.2f
        if (left) {
            leftStick.active = down
            logStick("L", x, y, down)
        } else if (right) {
            rightStick.active = down
            logStick("R", x, y, down)
        }
    }

    private fun logStick(side: String, x: Float, y: Float, down: Boolean) {
        if (down) {
            val dx = (x - (if (side == "L") leftStick.cx else rightStick.cx)) /
                (if (side == "L") leftStick.r else rightStick.r)
            val dy = (y - (if (side == "L") leftStick.cy else rightStick.cy)) /
                (if (side == "L") leftStick.r else rightStick.r)
            Log.d("TouchControls", "stick$side dx=%.2f dy=%.2f".format(dx, dy))
        }
    }

    private fun dist(x1: Float, y1: Float, x2: Float, y2: Float): Float {
        val dx = x1 - x2; val dy = y1 - y2
        return kotlin.math.sqrt(dx * dx + dy * dy)
    }
}
