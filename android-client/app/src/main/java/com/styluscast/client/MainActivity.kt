package com.styluscast.client

import android.app.AlertDialog
import android.content.Context
import android.content.pm.ActivityInfo
import android.net.wifi.WifiManager
import android.os.Bundle
import android.os.SystemClock
import android.text.InputType
import android.view.WindowManager
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.ArrayAdapter
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.net.InetAddress

/**
 * Stylus-Cast Android Client MainActivity.
 *
 * Responsibilities:
 * - Locks orientation to sensorLandscape and enters immersive edge-to-edge fullscreen.
 * - Requests unbuffered stylus dispatch on DecorView (`requestUnbufferedDispatch(SOURCE_STYLUS)`).
 * - Acquires `WifiManager.WIFI_MODE_FULL_LOW_LATENCY` lock and MulticastLock while active.
 * - Manages the zero-allocation `UdpSender` thread and `BeaconDiscovery` listener.
 * - Provides manual Mac IP override & Mac display aspect ratio selection via the status pill.
 */
class MainActivity : AppCompatActivity() {

    companion object {
        private const val PREFS_NAME = "styluscast_prefs"
        private const val KEY_MANUAL_IP = "manual_ip"
        private const val KEY_ASPECT_INDEX = "aspect_index"

        // The server beacons at 1Hz; treat the link as down after several missed beacons
        private const val BEACON_TIMEOUT_MS = 4000L
        private const val STATUS_POLL_MS = 1000L

        private val ASPECT_OPTIONS = arrayOf(
            "16:10 (MacBook Pro / Air)" to Pair(16f, 10f),
            "16:9 (External 4K / 1080p)" to Pair(16f, 9f),
            "3:2 (Productivity Display)" to Pair(3f, 2f),
            "21:9 (Ultrawide Display)" to Pair(21f, 9f)
        )
    }

    private lateinit var udpSender: UdpSender
    private lateinit var beaconDiscovery: BeaconDiscovery
    private lateinit var digitizerView: DigitizerView

    // Connection state below is only touched on the UI thread
    private var manualHost: String? = null
    private var manualHostInvalid = false
    private var lastBeaconMs = 0L
    private var shownStatus: String? = null

    private val statusPoll = object : Runnable {
        override fun run() {
            refreshStatus()
            digitizerView.postDelayed(this, STATUS_POLL_MS)
        }
    }

    private var wifiLock: WifiManager.WifiLock? = null
    private var multicastLock: WifiManager.MulticastLock? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        udpSender = UdpSender(StylusPacket.DEFAULT_UDP_PORT)
        digitizerView = DigitizerView(
            context = this,
            udpSender = udpSender,
            onStatusPillTapped = { showConfigurationDialog() }
        )
        setContentView(digitizerView)

        DigitizerView.requestUnbufferedStylus(window.decorView)

        beaconDiscovery = BeaconDiscovery(StylusPacket.DEFAULT_BEACON_PORT) { address ->
            runOnUiThread { onBeacon(address) }
        }

        restoreSavedPreferences()
        enterImmersiveFullscreen()
    }

    override fun onResume() {
        super.onResume()
        enterImmersiveFullscreen()
        acquireLowLatencyWifiLocks()
        udpSender.start()
        beaconDiscovery.start()
        digitizerView.removeCallbacks(statusPoll)
        digitizerView.post(statusPoll)
    }

    override fun onPause() {
        digitizerView.removeCallbacks(statusPoll)
        digitizerView.releasePen() // queued before stop(), which flushes the queue
        beaconDiscovery.stop()
        udpSender.stop()
        releaseWifiLocks()
        super.onPause()
    }

    override fun onDestroy() {
        beaconDiscovery.stop()
        udpSender.stop()
        releaseWifiLocks()
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            enterImmersiveFullscreen()
        }
    }

    private fun enterImmersiveFullscreen() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.hide(WindowInsetsCompat.Type.systemBars())
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }

    private fun acquireLowLatencyWifiLocks() {
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
            ?: return

        try {
            if (wifiLock == null) {
                wifiLock = wifiManager.createWifiLock(
                    WifiManager.WIFI_MODE_FULL_LOW_LATENCY,
                    "StylusCast::LowLatencyWifiLock"
                ).apply {
                    setReferenceCounted(false)
                }
            }
            if (wifiLock?.isHeld == false) {
                wifiLock?.acquire()
            }
        } catch (_: Exception) {
        }

        try {
            if (multicastLock == null) {
                multicastLock = wifiManager.createMulticastLock("StylusCast::BeaconMulticastLock").apply {
                    setReferenceCounted(false)
                }
            }
            if (multicastLock?.isHeld == false) {
                multicastLock?.acquire()
            }
        } catch (_: Exception) {
        }
    }

    private fun releaseWifiLocks() {
        try {
            if (wifiLock?.isHeld == true) {
                wifiLock?.release()
            }
        } catch (_: Exception) {
        }
        try {
            if (multicastLock?.isHeld == true) {
                multicastLock?.release()
            }
        } catch (_: Exception) {
        }
    }

    private fun restoreSavedPreferences() {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val manualIp = prefs.getString(KEY_MANUAL_IP, "")?.trim().orEmpty()
        val aspectIdx = prefs.getInt(KEY_ASPECT_INDEX, 0).coerceIn(0, ASPECT_OPTIONS.lastIndex)

        applyAspectSelection(aspectIdx)

        selectTarget(manualIp)
    }

    /** Uses [host] as a fixed target, or auto-discovery when it is empty. */
    private fun selectTarget(host: String) {
        digitizerView.releasePen()
        lastBeaconMs = 0L
        manualHostInvalid = false
        if (host.isEmpty()) {
            manualHost = null
            udpSender.setTargetInetAddress(null)
        } else {
            manualHost = host
            udpSender.setTargetHost(host) { resolved ->
                runOnUiThread {
                    if (manualHost == host) {
                        manualHostInvalid = resolved == null
                        refreshStatus()
                    }
                }
            }
        }
        refreshStatus()
    }

    private fun isBeaconFresh(): Boolean =
        lastBeaconMs != 0L && SystemClock.elapsedRealtime() - lastBeaconMs < BEACON_TIMEOUT_MS

    private fun onBeacon(address: InetAddress) {
        val target = udpSender.getTargetAddress()
        if (address != target) {
            // A manual target is fixed; an auto target is kept while its own beacon is alive,
            // so two Macs on one network cannot make the stream flip back and forth.
            if (manualHost != null || (target != null && isBeaconFresh())) return
            digitizerView.releasePen()
            udpSender.setTargetInetAddress(address)
        }
        lastBeaconMs = SystemClock.elapsedRealtime()
        refreshStatus()
    }

    private fun refreshStatus() {
        val host = manualHost
        val fresh = isBeaconFresh()
        val status = when {
            host != null && manualHostInvalid -> "Invalid address: $host"
            host != null && fresh -> "Manual IP: $host"
            host != null -> "Manual IP: $host (no beacon)"
            fresh -> "Connected: ${udpSender.getTargetAddress()?.hostAddress}"
            else -> "Searching on hotspot..."
        }
        if (status != shownStatus) {
            shownStatus = status
            digitizerView.updateConnectionStatus(fresh && !manualHostInvalid, status)
        }
    }

    private fun applyAspectSelection(index: Int) {
        val (title, ratioPair) = ASPECT_OPTIONS[index]
        val shortLabel = title.substringBefore(" ")
        digitizerView.setAspectRatio(ratioPair.first, ratioPair.second, shortLabel)
    }

    private fun showConfigurationDialog() {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val currentManualIp = prefs.getString(KEY_MANUAL_IP, "").orEmpty()
        val currentAspectIdx = prefs.getInt(KEY_ASPECT_INDEX, 0).coerceIn(0, ASPECT_OPTIONS.lastIndex)

        val container = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(56, 36, 56, 16)
        }

        val ipInput = EditText(this).apply {
            hint = "Auto-discover via UDP :9001 (or enter Mac IP e.g. 192.168.43.10)"
            setText(currentManualIp)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setSingleLine()
        }

        val aspectSpinner = Spinner(this).apply {
            adapter = ArrayAdapter(
                this@MainActivity,
                android.R.layout.simple_spinner_dropdown_item,
                ASPECT_OPTIONS.map { it.first }
            )
            setSelection(currentAspectIdx)
        }

        container.addView(ipInput)
        container.addView(aspectSpinner)

        AlertDialog.Builder(this)
            .setTitle("Stylus-Cast Connection & Aspect Ratio")
            .setView(container)
            .setPositiveButton("Apply") { _, _ ->
                val newIp = ipInput.text.toString().trim()
                val newAspectIdx = aspectSpinner.selectedItemPosition.coerceIn(0, ASPECT_OPTIONS.lastIndex)

                prefs.edit()
                    .putString(KEY_MANUAL_IP, newIp)
                    .putInt(KEY_ASPECT_INDEX, newAspectIdx)
                    .apply()

                applyAspectSelection(newAspectIdx)

                selectTarget(newIp)
                enterImmersiveFullscreen()
            }
            .setNeutralButton("Auto-Discover") { _, _ ->
                prefs.edit().putString(KEY_MANUAL_IP, "").apply()
                selectTarget("")
                enterImmersiveFullscreen()
            }
            .setNegativeButton("Cancel") { _, _ ->
                enterImmersiveFullscreen()
            }
            .show()
    }
}
