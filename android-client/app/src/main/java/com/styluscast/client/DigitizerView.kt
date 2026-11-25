package com.styluscast.client

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.os.Build
import android.os.SystemClock
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View

/**
 * Full-screen low-GPU-overhead digitizer view that:
 * 1. Requests unbuffered stylus dispatch (`requestUnbufferedDispatch(SOURCE_STYLUS)`).
 * 2. Filters strictly for `TOOL_TYPE_STYLUS` / `TOOL_TYPE_ERASER` (rejects accidental palm/finger touches).
 * 3. Uses the whole screen as the drawing region, or letterboxes it to a chosen Mac display aspect ratio.
 * 4. Unrolls all `0 until event.historySize` historical samples as `TYPE_MOVE` before the current sample.
 * 5. Allows finger taps ONLY on the top-right status/settings pill for manual IP override & aspect ratio selection.
 */
@SuppressLint("ViewConstructor") // only ever created in code by MainActivity
class DigitizerView(
    context: Context,
    private val udpSender: UdpSender,
    private val onStatusPillTapped: () -> Unit
) : View(context) {

    // Active drawing area; covers the whole view unless an aspect ratio is selected
    private val activeRect = RectF()
    private val statusPillRect = RectF()

    private var targetAspectRatio: Float = 0f // 0 = fill the whole view
    private var aspectLabel: String = "Full"
    private var statusText: String = "Searching on hotspot (UDP :9001)..."
    private var isConnected: Boolean = false

    // Minimal low-GPU-overhead paints (all pre-allocated)
    private val letterboxPaint = Paint().apply {
        color = Color.parseColor("#060709")
        style = Paint.Style.FILL
    }

    private val activeAreaFillPaint = Paint().apply {
        color = Color.parseColor("#0D1017")
        style = Paint.Style.FILL
    }

    private val borderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#2B3548")
        style = Paint.Style.STROKE
        strokeWidth = 3.0f
    }

    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#182030")
        style = Paint.Style.STROKE
        strokeWidth = 1.5f
        pathEffect = DashPathEffect(floatArrayOf(12f, 12f), 0f)
    }

    private val statusBgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#CC161B26")
        style = Paint.Style.FILL
    }

    private val statusDotPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#F59E0B")
        style = Paint.Style.FILL
    }

    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#94A3B8")
        textSize = 28.0f
    }

    init {
        isFocusable = true
        isFocusableInTouchMode = true
        requestUnbufferedStylus(this)
    }

    companion object {
        // Samsung firmware reports these instead of DOWN/UP/MOVE/CANCEL while the
        // S-Pen barrel button is held.
        private const val SAMSUNG_PEN_DOWN = 211
        private const val SAMSUNG_PEN_UP = 212
        private const val SAMSUNG_PEN_MOVE = 213
        private const val SAMSUNG_PEN_CANCEL = 214

        // Must stay well below the server's 500ms stroke timeout
        private const val HEARTBEAT_MS = 100L

        private const val NANOS_PER_MILLI = 1_000_000L

        private fun eventNanos(event: MotionEvent): Long =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                event.eventTimeNanos
            } else {
                event.eventTime * NANOS_PER_MILLI
            }

        private fun historicalNanos(event: MotionEvent, pos: Int): Long =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                event.getHistoricalEventTimeNanos(pos)
            } else {
                event.getHistoricalEventTime(pos) * NANOS_PER_MILLI
            }

        /**
         * Requests unbuffered stylus delivery. The source-based overload needs API 30; on
         * API 29 the per-gesture overload is used from onTouchEvent instead.
         */
        @SuppressLint("WrongConstant") // SOURCE_STYLUS includes SOURCE_CLASS_POINTER
        fun requestUnbufferedStylus(view: View) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                view.requestUnbufferedDispatch(InputDevice.SOURCE_STYLUS)
            }
        }
    }

    // Last transmitted contact sample, re-sent as a heartbeat while the pen is held still
    private var penDown = false
    private var lastButtons = 0
    private var lastX = 0
    private var lastY = 0
    private var lastPressure = 0
    private var lastTiltX = 0
    private var lastTiltY = 0
    private var lastSendMs = 0L
    private var fingerDownOnPill = false

    private val heartbeat = object : Runnable {
        override fun run() {
            if (!penDown) return
            if (SystemClock.uptimeMillis() - lastSendMs >= HEARTBEAT_MS) {
                send(
                    StylusPacket.TYPE_MOVE, lastButtons, lastX, lastY,
                    lastPressure, lastTiltX, lastTiltY,
                    SystemClock.uptimeMillis() * NANOS_PER_MILLI
                )
            }
            postDelayed(this, HEARTBEAT_MS)
        }
    }

    private fun send(
        type: Int, buttons: Int, x: Int, y: Int,
        pressure: Int, tiltX: Int, tiltY: Int, timeNanos: Long
    ) {
        lastButtons = buttons
        lastX = x
        lastY = y
        lastPressure = pressure
        lastTiltX = tiltX
        lastTiltY = tiltY
        lastSendMs = SystemClock.uptimeMillis()
        udpSender.enqueueEvent(type, buttons, x, y, pressure, tiltX, tiltY, timeNanos)
    }

    private fun setPenDown(down: Boolean) {
        if (penDown == down) return
        penDown = down
        removeCallbacks(heartbeat)
        if (down) {
            postDelayed(heartbeat, HEARTBEAT_MS)
        }
    }

    /** Lifts the pen on the Mac if a stroke is in progress (e.g. the app is being paused). */
    fun releasePen() {
        if (!penDown) return
        setPenDown(false)
        send(
            StylusPacket.TYPE_UP, lastButtons, lastX, lastY, 0, lastTiltX, lastTiltY,
            SystemClock.uptimeMillis() * NANOS_PER_MILLI
        )
    }

    override fun onDetachedFromWindow() {
        releasePen()
        super.onDetachedFromWindow()
    }

    /** Letterboxes the canvas to [widthRatio]:[heightRatio], or fills the view if either is 0. */
    fun setAspectRatio(widthRatio: Float, heightRatio: Float, label: String) {
        targetAspectRatio =
            if (widthRatio > 0f && heightRatio > 0f) widthRatio / heightRatio else 0f
        aspectLabel = label
        recomputeActiveRect(width, height)
        invalidate()
    }

    fun updateConnectionStatus(connected: Boolean, displayStatus: String) {
        isConnected = connected
        statusText = displayStatus
        borderPaint.color = if (connected) Color.parseColor("#1E40AF") else Color.parseColor("#2B3548")
        statusDotPaint.color = if (connected) Color.parseColor("#10B981") else Color.parseColor("#F59E0B")
        postInvalidateOnAnimation()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        recomputeActiveRect(w, h)
        // The canvas reaches the screen edges, so strokes there must not trigger the back gesture
        systemGestureExclusionRects = listOf(Rect(0, 0, w, h))
    }

    private fun recomputeActiveRect(viewWidth: Int, viewHeight: Int) {
        if (viewWidth <= 0 || viewHeight <= 0) return

        var activeW = viewWidth.toFloat()
        var activeH = viewHeight.toFloat()
        if (targetAspectRatio > 0f) {
            if (activeW / activeH > targetAspectRatio) {
                // Pillarbox left/right
                activeW = activeH * targetAspectRatio
            } else {
                // Letterbox top/bottom
                activeH = activeW / targetAspectRatio
            }
        }

        val left = (viewWidth - activeW) * 0.5f
        val top = (viewHeight - activeH) * 0.5f
        activeRect.set(left, top, left + activeW, top + activeH)

        // Position status pill at top-right inside active area, clear of rounded screen corners
        val pillInset = 40f
        val pillWidth = 580f.coerceAtMost(viewWidth * 0.65f)
        val pillHeight = 56f
        statusPillRect.set(
            activeRect.right - pillWidth - pillInset,
            activeRect.top + pillInset,
            activeRect.right - pillInset,
            activeRect.top + pillInset + pillHeight
        )
    }

    override fun onDraw(canvas: Canvas) {
        // 1. Dark letterbox background
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), letterboxPaint)

        // 2. Active digitizer region
        canvas.drawRect(activeRect, activeAreaFillPaint)

        // 3. Subtle rule-of-thirds guides for spatial orientation without looking at phone
        val thirdW = activeRect.width() / 3f
        val thirdH = activeRect.height() / 3f
        canvas.drawLine(
            activeRect.left + thirdW, activeRect.top,
            activeRect.left + thirdW, activeRect.bottom, gridPaint
        )
        canvas.drawLine(
            activeRect.left + thirdW * 2f, activeRect.top,
            activeRect.left + thirdW * 2f, activeRect.bottom, gridPaint
        )
        canvas.drawLine(
            activeRect.left, activeRect.top + thirdH,
            activeRect.right, activeRect.top + thirdH, gridPaint
        )
        canvas.drawLine(
            activeRect.left, activeRect.top + thirdH * 2f,
            activeRect.right, activeRect.top + thirdH * 2f, gridPaint
        )

        // 4. Active region border, inset so the stroke stays on screen
        val half = borderPaint.strokeWidth * 0.5f
        canvas.drawRect(
            activeRect.left + half, activeRect.top + half,
            activeRect.right - half, activeRect.bottom - half, borderPaint
        )

        // 5. Discreet connection status pill (tappable by finger to configure IP / aspect ratio)
        canvas.drawRoundRect(statusPillRect, 28f, 28f, statusBgPaint)
        val cy = statusPillRect.centerY()
        canvas.drawCircle(statusPillRect.left + 28f, cy, 8f, statusDotPaint)

        val label = "$statusText  |  $aspectLabel"
        val textY = cy - (textPaint.descent() + textPaint.ascent()) * 0.5f
        canvas.drawText(label, statusPillRect.left + 50f, textY, textPaint)
    }

    /**
     * Checks if pointer 0 is an S-Pen stylus or eraser tool.
     */
    private fun isStylusTool(event: MotionEvent): Boolean {
        val toolType = event.getToolType(0)
        return toolType == MotionEvent.TOOL_TYPE_STYLUS ||
            toolType == MotionEvent.TOOL_TYPE_ERASER
    }

    /**
     * Computes the wire button bitmask:
     * Bit 0 (0x01) = BUTTON_STYLUS_PRIMARY (barrel button)
     * Bit 1 (0x02) = TOOL_TYPE_ERASER or secondary button
     */
    private fun extractButtons(event: MotionEvent): Int {
        var mask = 0
        val btnState = event.buttonState
        if ((btnState and MotionEvent.BUTTON_STYLUS_PRIMARY) != 0) {
            mask = mask or StylusPacket.BUTTON_PRIMARY
        }
        if (event.getToolType(0) == MotionEvent.TOOL_TYPE_ERASER ||
            (btnState and MotionEvent.BUTTON_STYLUS_SECONDARY) != 0
        ) {
            mask = mask or StylusPacket.BUTTON_ERASER
        }
        return mask
    }

    private fun mapXToU16(rawX: Float): Int {
        val w = activeRect.width()
        if (w <= 0f) return 0
        val normX = ((rawX - activeRect.left) / w).coerceIn(0.0f, 1.0f)
        return StylusPacket.normalizeToU16(normX)
    }

    private fun mapYToU16(rawY: Float): Int {
        val h = activeRect.height()
        if (h <= 0f) return 0
        val normY = ((rawY - activeRect.top) / h).coerceIn(0.0f, 1.0f)
        return StylusPacket.normalizeToU16(normY)
    }

    override fun onHoverEvent(event: MotionEvent): Boolean {
        if (!isStylusTool(event)) {
            return false
        }

        val action = event.actionMasked
        if (action != MotionEvent.ACTION_HOVER_MOVE &&
            action != MotionEvent.ACTION_HOVER_ENTER &&
            action != MotionEvent.ACTION_HOVER_EXIT
        ) {
            return true
        }

        val buttons = extractButtons(event)

        // Unroll any historical hover points
        val historySize = event.historySize
        for (h in 0 until historySize) {
            val hx = mapXToU16(event.getHistoricalX(0, h))
            val hy = mapYToU16(event.getHistoricalY(0, h))
            val hTilt = event.getHistoricalAxisValue(MotionEvent.AXIS_TILT, 0, h)
            val hOrient = event.getHistoricalAxisValue(MotionEvent.AXIS_ORIENTATION, 0, h)
            val packedTilt = StylusPacket.computePlanarTiltPacked(hTilt, hOrient)

            send(
                StylusPacket.TYPE_HOVER, buttons, hx, hy, 0,
                StylusPacket.unpackTiltX(packedTilt), StylusPacket.unpackTiltY(packedTilt),
                historicalNanos(event, h)
            )
        }

        // Emit current hover point
        val cx = mapXToU16(event.getX(0))
        val cy = mapYToU16(event.getY(0))
        val cTilt = event.getAxisValue(MotionEvent.AXIS_TILT, 0)
        val cOrient = event.getAxisValue(MotionEvent.AXIS_ORIENTATION, 0)
        val packedTilt = StylusPacket.computePlanarTiltPacked(cTilt, cOrient)

        send(
            StylusPacket.TYPE_HOVER, buttons, cx, cy, 0,
            StylusPacket.unpackTiltX(packedTilt), StylusPacket.unpackTiltY(packedTilt),
            eventNanos(event)
        )

        return true
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!isStylusTool(event)) {
            // Reject palm rests and finger drawing completely;
            // allow a deliberate finger tap on the top-right status pill to open settings.
            // The touch must both start and end on the pill, so a lifting palm cannot open it.
            val onPill = statusPillRect.contains(event.x, event.y)
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> fingerDownOnPill = onPill
                MotionEvent.ACTION_UP -> {
                    val tapped = fingerDownOnPill && onPill
                    fingerDownOnPill = false
                    if (tapped) performClick()
                }
                MotionEvent.ACTION_CANCEL,
                MotionEvent.ACTION_POINTER_DOWN -> fingerDownOnPill = false
            }
            return true
        }
        fingerDownOnPill = false

        var buttons = extractButtons(event)
        val action = when (event.actionMasked) {
            SAMSUNG_PEN_DOWN -> MotionEvent.ACTION_DOWN
            SAMSUNG_PEN_UP -> MotionEvent.ACTION_UP
            SAMSUNG_PEN_MOVE -> MotionEvent.ACTION_MOVE
            SAMSUNG_PEN_CANCEL -> MotionEvent.ACTION_CANCEL
            else -> event.actionMasked
        }
        if (action != event.actionMasked) {
            buttons = buttons or StylusPacket.BUTTON_PRIMARY
        }

        if (action == MotionEvent.ACTION_DOWN && Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            requestUnbufferedDispatch(event)
        }

        // 1. First unroll all batched historical samples (0 until event.historySize) as TYPE_MOVE (2)
        if (action == MotionEvent.ACTION_MOVE) {
            val historySize = event.historySize
            for (h in 0 until historySize) {
                val hx = mapXToU16(event.getHistoricalX(0, h))
                val hy = mapYToU16(event.getHistoricalY(0, h))
                val hPressure = StylusPacket.normalizeToU16(event.getHistoricalPressure(0, h))
                val hTilt = event.getHistoricalAxisValue(MotionEvent.AXIS_TILT, 0, h)
                val hOrient = event.getHistoricalAxisValue(MotionEvent.AXIS_ORIENTATION, 0, h)
                val packedTilt = StylusPacket.computePlanarTiltPacked(hTilt, hOrient)

                send(
                    StylusPacket.TYPE_MOVE, buttons, hx, hy, hPressure,
                    StylusPacket.unpackTiltX(packedTilt), StylusPacket.unpackTiltY(packedTilt),
                    historicalNanos(event, h)
                )
            }
        }

        // 2. Map current event action to wire packet type
        val packetType = when (action) {
            MotionEvent.ACTION_DOWN -> StylusPacket.TYPE_DOWN
            MotionEvent.ACTION_MOVE -> StylusPacket.TYPE_MOVE
            MotionEvent.ACTION_UP,
            MotionEvent.ACTION_CANCEL -> StylusPacket.TYPE_UP
            else -> return true
        }
        setPenDown(packetType != StylusPacket.TYPE_UP)

        val cx = mapXToU16(event.getX(0))
        val cy = mapYToU16(event.getY(0))
        val cPressure = if (packetType == StylusPacket.TYPE_UP) {
            0
        } else {
            StylusPacket.normalizeToU16(event.getPressure(0))
        }
        val cTilt = event.getAxisValue(MotionEvent.AXIS_TILT, 0)
        val cOrient = event.getAxisValue(MotionEvent.AXIS_ORIENTATION, 0)
        val packedTilt = StylusPacket.computePlanarTiltPacked(cTilt, cOrient)

        send(
            packetType, buttons, cx, cy, cPressure,
            StylusPacket.unpackTiltX(packedTilt), StylusPacket.unpackTiltY(packedTilt),
            eventNanos(event)
        )

        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        onStatusPillTapped()
        return true
    }
}
