package com.penbridge.client

import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.SocketTimeoutException

/**
 * Listens on UDP port 9001 for the 1Hz "MAC_TABLET_SERVER" broadcast beacon
 * from the MacBook connected to the phone's 5 GHz Mobile Hotspot.
 *
 * [onBeacon] is invoked on the listener thread for every valid beacon.
 */
class BeaconDiscovery(
    private val beaconPort: Int = StylusPacket.DEFAULT_BEACON_PORT,
    private val onBeacon: (InetAddress) -> Unit
) {
    // Each start() gets its own session, so a quick stop()/start() can never
    // revive the previous listener thread.
    private class Session {
        @Volatile
        var running = true

        @Volatile
        var socket: DatagramSocket? = null
    }

    private var session: Session? = null

    fun start() {
        if (session != null) return
        val newSession = Session()
        session = newSession
        val thread = Thread({ runDiscoveryLoop(newSession) }, "PenBridge-BeaconDiscovery")
        thread.isDaemon = true
        thread.start()
    }

    fun stop() {
        val old = session ?: return
        session = null
        old.running = false
        old.socket?.close() // unblocks receive() immediately
    }

    private fun runDiscoveryLoop(current: Session) {
        while (current.running) {
            var socket: DatagramSocket? = null
            try {
                socket = DatagramSocket(null).apply {
                    reuseAddress = true
                    broadcast = true
                    soTimeout = 1000
                    bind(InetSocketAddress(beaconPort))
                }
                current.socket = socket
                receiveBeacons(current, socket)
            } catch (_: Exception) {
                // Bind or network failure, or socket closed by stop()
            } finally {
                current.socket = null
                socket?.close()
            }
            if (current.running) {
                Thread.sleep(500) // retry, e.g. while the hotspot interface comes up
            }
        }
    }

    private fun receiveBeacons(current: Session, socket: DatagramSocket) {
        val recvBuf = ByteArray(64)
        val packet = DatagramPacket(recvBuf, recvBuf.size)
        val expectedBytes = StylusPacket.BEACON_PAYLOAD.toByteArray(Charsets.US_ASCII)

        while (current.running) {
            try {
                packet.length = recvBuf.size
                socket.receive(packet)
            } catch (_: SocketTimeoutException) {
                continue // so the loop can check the running flag
            }
            if (packet.length == expectedBytes.size && matchesPayload(recvBuf, expectedBytes)) {
                val sender = packet.address
                if (sender != null && !sender.isLoopbackAddress) {
                    onBeacon(sender)
                }
            }
        }
    }

    private fun matchesPayload(buf: ByteArray, expected: ByteArray): Boolean {
        for (i in expected.indices) {
            if (buf[i] != expected[i]) return false
        }
        return true
    }
}
