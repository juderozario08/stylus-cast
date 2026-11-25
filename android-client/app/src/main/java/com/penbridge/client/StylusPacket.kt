package com.penbridge.client

import kotlin.math.atan
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.tan

/**
 * 16-Byte Little-Endian Binary Wire Protocol definition and zero-allocation encoder.
 *
 * Byte Layout:
 *  Offset 0  (1 B, uint8_t) : type      (0=Hover, 1=Down, 2=Move, 3=Up/Cancel)
 *  Offset 1  (1 B, uint8_t) : buttons   (Bit 0=Barrel primary, Bit 1=Eraser)
 *  Offset 2  (2 B, uint16_t): x         (Normalized X [0..65535])
 *  Offset 4  (2 B, uint16_t): y         (Normalized Y [0..65535])
 *  Offset 6  (2 B, uint16_t): pressure  (Normalized Pressure [0..65535], 0 on hover)
 *  Offset 8  (1 B, int8_t)  : tiltX     (Degrees [-90..90], positive = pen leans right)
 *  Offset 9  (1 B, int8_t)  : tiltY     (Degrees [-90..90], positive = pen leans toward the user)
 *  Offset 10 (4 B, uint32_t): seq       (Monotonically increasing sequence counter)
 *  Offset 14 (2 B, uint16_t): timestamp (sample time in 100us ticks, wraps; 0 = none)
 */
object StylusPacket {
    const val PACKET_SIZE = 16

    const val TYPE_HOVER: Int = 0
    const val TYPE_DOWN: Int = 1
    const val TYPE_MOVE: Int = 2
    const val TYPE_UP: Int = 3

    const val BUTTON_PRIMARY: Int = 0x01
    const val BUTTON_ERASER: Int = 0x02

    const val DEFAULT_UDP_PORT = 9000
    const val DEFAULT_BEACON_PORT = 9001
    const val BEACON_PAYLOAD = "MAC_TABLET_SERVER"

    /**
     * Writes a 16-byte little-endian packet directly into [buffer] at [offset]
     * with zero heap allocations.
     */
    fun writeToBuffer(
        buffer: ByteArray,
        offset: Int,
        type: Int,
        buttons: Int,
        xU16: Int,
        yU16: Int,
        pressureU16: Int,
        tiltXDeg: Int,
        tiltYDeg: Int,
        seq: Int,
        timestamp: Int
    ) {
        buffer[offset] = (type and 0xFF).toByte()
        buffer[offset + 1] = (buttons and 0xFF).toByte()

        // uint16_t x (little-endian)
        buffer[offset + 2] = (xU16 and 0xFF).toByte()
        buffer[offset + 3] = ((xU16 ushr 8) and 0xFF).toByte()

        // uint16_t y (little-endian)
        buffer[offset + 4] = (yU16 and 0xFF).toByte()
        buffer[offset + 5] = ((yU16 ushr 8) and 0xFF).toByte()

        // uint16_t pressure (little-endian)
        buffer[offset + 6] = (pressureU16 and 0xFF).toByte()
        buffer[offset + 7] = ((pressureU16 ushr 8) and 0xFF).toByte()

        // int8_t tiltX, tiltY
        buffer[offset + 8] = tiltXDeg.coerceIn(-90, 90).toByte()
        buffer[offset + 9] = tiltYDeg.coerceIn(-90, 90).toByte()

        // uint32_t seq (little-endian)
        buffer[offset + 10] = (seq and 0xFF).toByte()
        buffer[offset + 11] = ((seq ushr 8) and 0xFF).toByte()
        buffer[offset + 12] = ((seq ushr 16) and 0xFF).toByte()
        buffer[offset + 13] = ((seq ushr 24) and 0xFF).toByte()

        // uint16_t timestamp (little-endian)
        buffer[offset + 14] = (timestamp and 0xFF).toByte()
        buffer[offset + 15] = ((timestamp ushr 8) and 0xFF).toByte()
    }

    /** Converts an event time to 16-bit 100us ticks. 0 is reserved for "no timestamp". */
    fun toTimestamp(timeNanos: Long): Int {
        val ticks = ((timeNanos / 100_000L) and 0xFFFFL).toInt()
        return if (ticks == 0) 1 else ticks
    }

    /**
     * Converts normalized float [0.0f..1.0f] to uint16 [0..65535].
     */
    fun normalizeToU16(value: Float): Int {
        val clamped = value.coerceIn(0.0f, 1.0f)
        return (clamped * 65535.0f).roundToInt().coerceIn(0, 65535)
    }

    /**
     * Projects Android's spherical stylus tilt (AXIS_TILT in [0..pi/2] from vertical)
     * and azimuth (AXIS_ORIENTATION in [-pi..pi], 0 = pointing up, +pi/2 = pointing right)
     * into Wacom planar tiltX and tiltY angles in degrees [-90..90].
     *
     * AXIS_ORIENTATION is the direction the tip points, i.e. opposite to the lean: AOSP
     * derives it as atan2(-sin(tiltX), sin(tiltY)), which this inverts.
     *
     * Packaged into a single Int (high 16 bits = tiltX, low 16 bits = tiltY)
     * to avoid Pair allocation on the hot path.
     */
    fun computePlanarTiltPacked(tiltRad: Float, orientationRad: Float): Int {
        if (tiltRad <= 0.001f) {
            return 0
        }
        val clampedTilt = tiltRad.coerceIn(0.0f, 1.553343f) // ~89 degrees max tan
        val tanTilt = tan(clampedTilt.toDouble())
        val sinAz = sin(orientationRad.toDouble())
        val cosAz = cos(orientationRad.toDouble())

        val tiltXDeg = Math.toDegrees(atan(-tanTilt * sinAz)).roundToInt().coerceIn(-90, 90)
        val tiltYDeg = Math.toDegrees(atan(tanTilt * cosAz)).roundToInt().coerceIn(-90, 90)

        return (tiltXDeg shl 16) or (tiltYDeg and 0xFFFF)
    }

    fun unpackTiltX(packed: Int): Int = packed shr 16
    fun unpackTiltY(packed: Int): Int = (packed.toShort()).toInt()
}
