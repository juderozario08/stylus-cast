package com.styluscast.client

import android.os.Process
import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference
import java.util.concurrent.locks.LockSupport
import java.util.concurrent.locks.ReentrantLock

/**
 * Zero-allocation stylus packet sender backed by a lock-free Single-Producer Single-Consumer
 * (SPSC) ring buffer.
 *
 * Supports two transports:
 * - USB (TCP to 127.0.0.1:<port> via `adb reverse`): zero packet loss, no Wi-Fi/hotspot needed.
 * - Wi-Fi / LAN (UDP with DSCP EF 0xB8): low-latency wireless fallback.
 */
class PacketSender(
    private val targetPort: Int = StylusPacket.DEFAULT_PORT,
    private val onUsbStateChanged: (Boolean) -> Unit
) {
    enum class TransportMode {
        AUTO,
        USB_ONLY,
        WIFI_ONLY
    }

    companion object {
        // Must be a power of 2 for bitwise mask indexing
        private const val RING_CAPACITY = 1024
        private const val RING_MASK = RING_CAPACITY - 1
        private const val USB_CONNECT_TIMEOUT_MS = 250
        private const val USB_PROBE_INTERVAL_NS = 800_000_000L
    }

    private val ringBuffer = ByteArray(RING_CAPACITY * StylusPacket.PACKET_SIZE)
    private val writeIndex = AtomicLong(0L)
    private val readIndex = AtomicLong(0L)

    private val modeRef = AtomicReference(TransportMode.AUTO)
    private val targetAddressRef = AtomicReference<InetAddress?>(null)
    private val targetGeneration = AtomicInteger(0)

    private val usbSocketRef = AtomicReference<Socket?>(null)
    private val usbConnectedRef = AtomicBoolean(false)

    // Held by the active consumer so a restarted sender never overlaps the previous one
    private val consumerLock = ReentrantLock()

    private class Session {
        @Volatile
        var running = true

        @Volatile
        var sendThread: Thread? = null

        @Volatile
        var usbThread: Thread? = null
    }

    @Volatile
    private var session: Session? = null
    private var seqCounter: Int = 1

    fun setTransportMode(mode: TransportMode) {
        val prev = modeRef.getAndSet(mode)
        if (prev == mode) return
        if (mode == TransportMode.WIFI_ONLY) {
            closeUsbSocket()
        }
        session?.usbThread?.let { LockSupport.unpark(it) }
        session?.sendThread?.let { LockSupport.unpark(it) }
    }

    fun getTransportMode(): TransportMode = modeRef.get()

    fun isUsbConnected(): Boolean = usbConnectedRef.get()

    fun nudgeUsbProbe() {
        session?.usbThread?.let { LockSupport.unpark(it) }
    }

    /**
     * Resolves [host] off the UI thread and makes it the Wi-Fi target. The previous target is
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

        val sendThread = Thread({
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_DISPLAY)
            consumerLock.lock()
            try {
                runSendLoop(newSession)
            } finally {
                consumerLock.unlock()
            }
        }, "StylusCast-Sender")
        sendThread.isDaemon = true

        val usbThread = Thread({
            runUsbMonitorLoop(newSession)
        }, "StylusCast-UsbMonitor")
        usbThread.isDaemon = true

        newSession.sendThread = sendThread
        newSession.usbThread = usbThread
        session = newSession

        sendThread.start()
        usbThread.start()
    }

    /** Stops the sender after it has flushed everything already queued. */
    fun stop() {
        val old = session ?: return
        session = null
        old.running = false
        closeUsbSocket()
        old.usbThread?.let { LockSupport.unpark(it) }
        old.sendThread?.let { LockSupport.unpark(it) }
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
        if (session == null) return
        val mode = modeRef.get()
        val hasUsb = usbConnectedRef.get() && mode != TransportMode.WIFI_ONLY
        val hasWifi = targetAddressRef.get() != null && mode != TransportMode.USB_ONLY
        if (!hasUsb && !hasWifi) return

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
        session?.sendThread?.let { LockSupport.unpark(it) }
    }

    private fun closeUsbSocket(target: Socket? = null) {
        val sock = if (target != null) {
            if (usbSocketRef.compareAndSet(target, null)) target else null
        } else {
            usbSocketRef.getAndSet(null)
        }
        val wasConnected = usbConnectedRef.getAndSet(false)
        if (sock != null) {
            try {
                sock.close()
            } catch (_: Exception) {
            }
        }
        if (wasConnected) {
            onUsbStateChanged(false)
        }
    }

    private fun runUsbMonitorLoop(current: Session) {
        val loopback = InetAddress.getByAddress(byteArrayOf(127, 0, 0, 1))

        while (current.running) {
            if (modeRef.get() == TransportMode.WIFI_ONLY) {
                closeUsbSocket()
                LockSupport.parkNanos(USB_PROBE_INTERVAL_NS)
                continue
            }

            var sock: Socket? = null
            try {
                val candidate = Socket()
                candidate.connect(InetSocketAddress(loopback, targetPort), USB_CONNECT_TIMEOUT_MS)
                if (!current.running || modeRef.get() == TransportMode.WIFI_ONLY) {
                    candidate.close()
                    continue
                }
                candidate.tcpNoDelay = true
                candidate.sendBufferSize = 64 * 1024
                sock = candidate
                usbSocketRef.set(candidate)
                usbConnectedRef.set(true)
                onUsbStateChanged(true)
                current.sendThread?.let { LockSupport.unpark(it) }

                // Block on read so EOF or USB cable removal is detected immediately even while idle
                val input = candidate.getInputStream()
                val buf = ByteArray(16)
                while (current.running && modeRef.get() != TransportMode.WIFI_ONLY) {
                    if (input.read(buf) < 0) break
                }
            } catch (_: Exception) {
                // ADB reverse not active yet or server stopped
            } finally {
                if (sock != null) {
                    closeUsbSocket(sock)
                }
            }

            if (current.running) {
                LockSupport.parkNanos(USB_PROBE_INTERVAL_NS)
            }
        }
    }

    private fun runSendLoop(current: Session) {
        var udpSocket: DatagramSocket? = null
        // Pre-allocate buffers once for zero-allocation hot-path sending
        val packetBytes = ByteArray(StylusPacket.PACKET_SIZE)
        val datagram = DatagramPacket(packetBytes, StylusPacket.PACKET_SIZE)
        val tcpBatchBytes = ByteArray(RING_CAPACITY * StylusPacket.PACKET_SIZE)

        try {
            udpSocket = DatagramSocket()
            // DSCP EF (46 << 2 = 0xB8): mapped to a prioritized Wi-Fi WMM (video/voice) queue
            try {
                udpSocket.trafficClass = 0xB8
                udpSocket.sendBufferSize = 64 * 1024
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

                val mode = modeRef.get()
                val usbSock = if (mode != TransportMode.WIFI_ONLY) usbSocketRef.get() else null
                if (usbSock != null) {
                    // Drain all queued slots in a single contiguous batch over USB TCP
                    val count = (w - r).toInt().coerceAtMost(RING_CAPACITY)
                    val startSlot = (r.toInt()) and RING_MASK
                    val firstChunk = (RING_CAPACITY - startSlot).coerceAtMost(count)
                    val firstBytes = firstChunk * StylusPacket.PACKET_SIZE
                    System.arraycopy(ringBuffer, startSlot * StylusPacket.PACKET_SIZE, tcpBatchBytes, 0, firstBytes)
                    val remaining = count - firstChunk
                    if (remaining > 0) {
                        System.arraycopy(ringBuffer, 0, tcpBatchBytes, firstBytes, remaining * StylusPacket.PACKET_SIZE)
                    }
                    r += count
                    readIndex.lazySet(r)

                    try {
                        val out = usbSock.getOutputStream()
                        out.write(tcpBatchBytes, 0, count * StylusPacket.PACKET_SIZE)
                        out.flush()
                    } catch (_: IOException) {
                        closeUsbSocket(usbSock)
                    }
                    continue
                }

                val targetAddr = if (mode != TransportMode.USB_ONLY) targetAddressRef.get() else null
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
                            udpSocket.send(datagram)
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
                udpSocket?.close()
            } catch (_: Exception) {
            }
        }
    }
}
