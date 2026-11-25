package com.penbridge.client

import android.os.Process
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference
import java.util.concurrent.locks.LockSupport
import java.util.concurrent.locks.ReentrantLock

/**
 * Zero-allocation UDP sender backed by a lock-free Single-Producer Single-Consumer (SPSC)
 * ring buffer and a dedicated high-priority network thread.
 *
 * Touch and hover callbacks on the UI thread write directly into pre-allocated byte slots
 * without creating any ByteArray, DatagramPacket, or Runnable objects.
 */
class UdpSender(
    private val targetPort: Int = StylusPacket.DEFAULT_UDP_PORT
) {
    companion object {
        // Must be a power of 2 for bitwise mask indexing
        private const val RING_CAPACITY = 1024
        private const val RING_MASK = RING_CAPACITY - 1
    }

    private val ringBuffer = ByteArray(RING_CAPACITY * StylusPacket.PACKET_SIZE)
    private val writeIndex = AtomicLong(0L)
    private val readIndex = AtomicLong(0L)

    private val targetAddressRef = AtomicReference<InetAddress?>(null)
    private val targetGeneration = AtomicInteger(0)

    // Held by the active consumer so a restarted sender never overlaps the previous one
    private val consumerLock = ReentrantLock()

    private class Session {
        @Volatile
        var running = true

        @Volatile
        var thread: Thread? = null
    }

    @Volatile
    private var session: Session? = null
    private var seqCounter: Int = 1

    /**
     * Resolves [host] off the UI thread and makes it the target. The previous target is
     * cleared immediately. [onResolved] runs on the resolver thread with the address, or
     * null if it is invalid; it is skipped if the target was changed again in the meantime.
     */
    fun setTargetHost(host: String, onResolved: (InetAddress?) -> Unit) {
        val generation = targetGeneration.incrementAndGet()
        targetAddressRef.set(null)
        Thread {
            val resolved = try {
                InetAddress.getByName(host.trim())
            } catch (_: Exception) {
                null
            }
            synchronized(targetGeneration) {
                if (targetGeneration.get() != generation) return@Thread
                targetAddressRef.set(resolved)
            }
            onResolved(resolved)
        }.start()
    }

    fun setTargetInetAddress(address: InetAddress?) {
        synchronized(targetGeneration) {
            targetGeneration.incrementAndGet()
            targetAddressRef.set(address)
        }
    }

    fun getTargetAddress(): InetAddress? = targetAddressRef.get()

    fun start() {
        if (session != null) return
        val newSession = Session()
        val thread = Thread({
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_DISPLAY)
            consumerLock.lock()
            try {
                runSendLoop(newSession)
            } finally {
                consumerLock.unlock()
            }
        }, "PenBridge-UdpSender")
        thread.isDaemon = true
        newSession.thread = thread
        session = newSession
        thread.start()
    }

    /** Stops the sender after it has flushed everything already queued. */
    fun stop() {
        val old = session ?: return
        session = null
        old.running = false
        old.thread?.let { LockSupport.unpark(it) }
    }

    /**
     * Enqueues a 16-byte stylus event from the UI thread with zero heap allocations.
     * If the ring buffer is full the sample is dropped: only the consumer may advance the
     * read index, otherwise it could transmit a slot while it is being rewritten. The
     * sequence number still advances so the server counts the loss.
     */
    fun enqueueEvent(
        type: Int,
        buttons: Int,
        xU16: Int,
        yU16: Int,
        pressureU16: Int,
        tiltXDeg: Int,
        tiltYDeg: Int,
        timeNanos: Long
    ) {
        if (targetAddressRef.get() == null || session == null) return

        val currentWrite = writeIndex.get()
        val currentRead = readIndex.get()

        val seq = seqCounter++

        if (currentWrite - currentRead >= RING_CAPACITY) return

        val slotOffset = ((currentWrite.toInt()) and RING_MASK) * StylusPacket.PACKET_SIZE

        StylusPacket.writeToBuffer(
            buffer = ringBuffer,
            offset = slotOffset,
            type = type,
            buttons = buttons,
            xU16 = xU16,
            yU16 = yU16,
            pressureU16 = if (type == StylusPacket.TYPE_HOVER) 0 else pressureU16,
            tiltXDeg = tiltXDeg,
            tiltYDeg = tiltYDeg,
            seq = seq,
            timestamp = StylusPacket.toTimestamp(timeNanos)
        )

        writeIndex.lazySet(currentWrite + 1)
        session?.thread?.let { LockSupport.unpark(it) }
    }

    private fun runSendLoop(current: Session) {
        var socket: DatagramSocket? = null
        // Pre-allocate a single 16-byte payload array and DatagramPacket for the entire lifecycle
        val packetBytes = ByteArray(StylusPacket.PACKET_SIZE)
        val datagram = DatagramPacket(packetBytes, StylusPacket.PACKET_SIZE)

        try {
            socket = DatagramSocket()
            // DSCP EF (46 << 2 = 0xB8): mapped to a prioritized Wi-Fi WMM (video/voice) queue
            try {
                socket.trafficClass = 0xB8
                socket.sendBufferSize = 64 * 1024
            } catch (_: Exception) {
                // Some OEM kernels restrict trafficClass; safe to ignore
            }

            while (true) {
                // Read the flag before the indices so nothing queued before stop() is missed
                val running = current.running
                var r = readIndex.get()
                val w = writeIndex.get()

                if (r >= w) {
                    if (!running) break
                    // Park briefly until UI thread unparks us on the very next stylus sample
                    LockSupport.parkNanos(50_000_000L) // 50ms max park
                    continue
                }

                val targetAddr = targetAddressRef.get()
                while (r < w) {
                    val slotOffset = ((r.toInt()) and RING_MASK) * StylusPacket.PACKET_SIZE
                    System.arraycopy(ringBuffer, slotOffset, packetBytes, 0, StylusPacket.PACKET_SIZE)
                    r++
                    // Free the slot only after it has been copied out
                    readIndex.lazySet(r)

                    if (targetAddr != null) {
                        datagram.address = targetAddr
                        datagram.port = targetPort
                        datagram.length = StylusPacket.PACKET_SIZE
                        try {
                            socket.send(datagram)
                        } catch (_: Exception) {
                            // Transient network error over hotspot; drop packet without stalling
                        }
                    }
                }
            }
        } catch (_: Exception) {
            // Socket creation failure on shutdown
        } finally {
            try {
                socket?.close()
            } catch (_: Exception) {
            }
        }
    }
}
