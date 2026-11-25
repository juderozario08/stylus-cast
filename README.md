# Stylus-Cast: Ultra-Low-Latency Samsung S-Pen to macOS Digitizer

Stylus-Cast turns a Samsung Galaxy phone with an S-Pen into an offline, wireless, low-latency absolute-coordinate Wacom-style drawing and handwriting tablet for macOS.

- **Direct Local Link:** Uses a direct **5 GHz Mobile Hotspot** hosted on the Samsung phone so packets travel directly between the phone and MacBook with zero external router hops and zero internet dependency.
- **16-Byte Binary UDP Wire Protocol:** Connectionless little-endian binary datagrams carry normalized coordinates (`x`, `y`), 16-bit pressure, planar tilt angles (`tiltX`, `tiltY`), S-Pen barrel button/eraser flags, a monotonic sequence counter (`seq`), and a sample timestamp.
- **240Hz–360Hz S-Pen Capture:** Requests unbuffered stylus dispatch (`requestUnbufferedDispatch(SOURCE_STYLUS)`), rejects palm and finger touches (`TOOL_TYPE_STYLUS` / `TOOL_TYPE_ERASER`), unrolls every historical `MotionEvent` sample (`0 until event.historySize`), and streams packets from a zero-allocation lock-free ring buffer with `WIFI_MODE_FULL_LOW_LATENCY`.
- **Even Strokes over Wi-Fi:** The server replays samples at the spacing the pen produced them, behind a small delay that adapts between strokes (6–45 ms), fills gaps from lost packets with a curve, and lightly smooths tremor. Tune with `--delay` and `--smooth`.
- **Native Wacom Tablet Events on macOS:** Injects CoreGraphics `CGEvent` events with `kCGEventMouseSubtypeTabletPoint`, `kCGMouseEventPressure`, `kCGTabletEventPointPressure`, `kCGTabletEventTiltX`, and `kCGTabletEventTiltY` at `kCGHIDEventTap`, plus tablet proximity events so apps can tell the pen tip from the eraser. Holding the barrel button when the pen touches down makes that stroke a right-click/right-drag.

---

## Repository Structure

```text
.
├── macos-server/
│   ├── CMakeLists.txt           # The build definition; exports compile_commands.json
│   ├── Makefile                 # Thin wrapper around CMake
│   ├── include/stylus_cast/     # Declarations only
│   ├── src/
│   │   ├── main.cpp             # Entry point
│   │   ├── config.cpp           # Command-line options
│   │   ├── server.cpp           # UDP receive / playout loop and statistics
│   │   ├── protocol.cpp         # 16-byte wire packet decoding and sequencing
│   │   ├── pen_sample.cpp       # Full-precision sample type
│   │   ├── stream_filter.cpp    # Session ownership, stale filtering, loss counting
│   │   ├── jitter_buffer.cpp    # Even-timing playout, adaptive delay, gap filling
│   │   ├── stroke_smoother.cpp  # One Euro stroke smoothing
│   │   ├── pen_state.cpp        # Press/drag/release state machine
│   │   ├── tablet_events.cpp    # CGEvent construction (pressure, tilt, proximity)
│   │   ├── injector.cpp         # Posts events; Accessibility check
│   │   ├── beacon.cpp           # 1Hz discovery broadcast
│   │   ├── display.cpp          # Display enumeration and letterboxing
│   │   ├── realtime.cpp         # Real-time thread policy and clock
│   │   └── trace.cpp            # Optional CSV recording
│   └── tests/
├── android-client/
│   ├── gradlew, gradle/             # Gradle 8.14.3 wrapper (runs its daemon on JDK 21)
│   ├── settings.gradle.kts
│   ├── build.gradle.kts
│   ├── gradle.properties
│   └── app/
│       ├── build.gradle.kts
│       └── src/main/
│           ├── AndroidManifest.xml
│           ├── java/com/styluscast/client/
│           │   ├── StylusPacket.kt      # 16-byte zero-allocation binary encoder & planar tilt math
│           │   ├── UdpSender.kt         # Lock-free SPSC ring buffer & high-priority UDP sender thread
│           │   ├── BeaconDiscovery.kt   # UDP :9001 listener for "MAC_TABLET_SERVER" broadcast beacon
│           │   ├── DigitizerView.kt     # Letterboxed canvas, historical sample unrolling & palm rejection
│           │   └── MainActivity.kt      # Immersive fullscreen, WIFI_MODE_FULL_LOW_LATENCY & settings dialog
│           └── res/values/
│               ├── strings.xml
│               └── themes.xml
└── README.md
```

---

## Step-by-Step Setup Guide

### 1. Configure the 5 GHz Mobile Hotspot on Samsung Galaxy
1. On your Samsung Galaxy phone, open **Settings > Connections > Mobile Hotspot and Tethering > Mobile Hotspot**.
2. Tap **Band** (or **Configure**) and select **5 GHz** (or **5 GHz preferred**).
   - *Tip:* You can turn **Mobile Data OFF** if you want a pure offline LAN; the 5 GHz Wi-Fi hotspot still creates the local subnet between the phone and MacBook.
3. Enable **Mobile Hotspot** and connect your MacBook to the phone's Wi-Fi network.

### 2. Build & Run the macOS Server (`macos-server/`)

#### Option A: Build with `make`
```bash
cd macos-server
make
make test
./stylus-cast-server --stats
```

#### Option B: Build with `cmake`
```bash
cd macos-server
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/stylus-cast-server --stats
```

Both options write `build/compile_commands.json`, which clangd finds automatically; `make` also links it at `macos-server/compile_commands.json`.

#### CLI Options
```text
./stylus-cast-server [options]

  --delay <ms|auto>      Constant playout delay that hides Wi-Fi jitter; 0 = off (default: auto)
  --smooth <0-10>        Stroke smoothing; 0 = off (default: 4)
  --port <port>          UDP listening port for 16-byte stylus packets (default: 9000)
  --beacon-port <port>   UDP broadcast port for 1Hz discovery beacon (default: 9001)
  --display <index>      Target display index (default: 0 = main display)
  --list-displays        List active displays and exit
  --aspect <W:H>         Letterbox target display mapping to W:H aspect ratio (e.g., 16:10)
  --client <ip>          Only accept stylus packets from this IPv4 address
  --stats                Print packet rate, jitter and loss every 2 seconds
  --trace <file>         Record input and output timing to a CSV file for diagnosis
  --verbose, -v          Log stylus Down/Up transitions and discovery events
  --prompt-accessibility Prompt macOS System Settings if Accessibility is not granted
  --dry-run              Receive and validate packets without injecting CGEvents
```

### 3. Grant macOS Accessibility Permission
CoreGraphics `CGEventPost(kCGHIDEventTap, ...)` requires macOS Accessibility authorization:
1. Open **System Settings > Privacy & Security > Accessibility**.
2. Enable your terminal app (**Terminal**, **iTerm2**, or **VS Code**) or add the `stylus-cast-server` binary directly.
3. Alternatively, run `./stylus-cast-server --prompt-accessibility` to trigger the macOS system prompt automatically.

### 4. Build & Install the Android Client (`android-client/`)
1. Open `android-client/` in **Android Studio** (or build via Gradle CLI with `./gradlew assembleDebug`; needs JDK 21 installed and Android SDK platform 34).
2. Connect your Samsung Galaxy phone over USB with **USB Debugging** enabled and install the app (`Run 'app'` or `adb install -r app/build/outputs/apk/debug/app-debug.apk`).
3. Launch **Stylus-Cast** on the phone:
   - The app locks to **landscape**, enters edge-to-edge fullscreen, and outlines the active letterboxed drawing area matching your Mac's `16:10` display aspect ratio.
   - Within 1 second of starting `./stylus-cast-server`, the status indicator in the top-right corner switches from `Searching on hotspot...` to `Connected: <Mac IP>`.
   - **Manual IP Override / Aspect Ratio:** Tap the top-right status pill with your **finger** (finger touches are ignored for drawing so palm rests never move the cursor) to manually enter the Mac's IP address or switch aspect ratios (`16:10`, `16:9`, `3:2`, `21:9`). With a manual IP the pill turns green only once that Mac's beacon is heard; packets are sent either way.

---

## Running a Quick Packet-Rate & Loss Check

1. Start the macOS server with live statistics enabled:
   ```bash
   ./macos-server/stylus-cast-server --stats --verbose
   ```
2. On your Samsung Galaxy phone, hover and draw rapid circles with the S-Pen.
3. Observe the terminal output on your Mac:
   ```text
   [Client] Active stream from 192.168.43.1 (initial seq=1)
   [Stylus] DOWN seq=42 x=18420 y=29104 pressure=24510 tilt=(12,-18) buttons=0x0
   [Stats] Rate: 240.5 pkt/s | Total: 481 | Lost: 0 | Stale: 0 | Malformed: 0 | Rejected: 0 | Jitter ms p50/p95/p99/max: 1.2/4.8/9.6/21.0 | Delay: 11.3ms | Late: 0 | Filled: 0 | Last: type=2 pos=(34192,31040) p=0.64
   ```
   - On S-Pen devices with 240Hz–360Hz digitizers, historical sample unrolling delivers **240–360 packets/sec** during active strokes with `Lost: 0`.
   - `Lost` counts missing sequence numbers, `Stale` late or duplicate packets, `Malformed` wrong sizes or unknown types, and `Rejected` packets from other clients. `Jitter` is how unevenly packets arrive, `Delay` the playout delay in use, `Late` samples the delay could not hide, and `Filled` points interpolated across gaps. This check measures rate and loss; it does not measure latency.

### If strokes still look uneven
- Turn off Low Power Mode on the Mac and Battery Saver on the phone, and keep the two close together.
- AirDrop's radio (AWDL) makes the Mac's Wi-Fi hop channels. `sudo ifconfig awdl0 down` stops it (AirDrop and Sidecar stop working) until `sudo ifconfig awdl0 up` or a restart.
- Raise `--delay` (e.g. `60`) if `Late` keeps growing; raise `--smooth` for shaky lines.
- Record a problem with `--trace trace.csv`.

### Link loss and security
- While the pen touches, the client re-sends its position every 100 ms even if held still. If the server hears nothing for 500 ms mid-stroke it releases the button.
- The first client owns the stream until it has been silent for 1.5 s; other senders are rejected. Packets are **not authenticated or encrypted**, so use a private hotspot, and pass `--client <phone IP>` to restrict input to your phone.

---

## Binary Wire Protocol Reference (16 Bytes, Little-Endian)

| Offset | Size | Type | Field | Description & Encoding |
| --- | --- | --- | --- | --- |
| `0` | 1 B | `uint8_t` | `type` | `0` = Hover (`ACTION_HOVER_MOVE`), `1` = Down (`ACTION_DOWN`), `2` = Move (`ACTION_MOVE` / historical), `3` = Up/Cancel (`ACTION_UP` / `ACTION_CANCEL`) |
| `1` | 1 B | `uint8_t` | `buttons` | Bit `0` (`0x01`): S-Pen primary barrel button (`BUTTON_STYLUS_PRIMARY`). Bit `1` (`0x02`): Eraser tool (`TOOL_TYPE_ERASER`, or the secondary stylus button). |
| `2` | 2 B | `uint16_t` | `x` | Normalized horizontal coordinate inside letterboxed active area mapped to `0..65535` |
| `4` | 2 B | `uint16_t` | `y` | Normalized vertical coordinate inside letterboxed active area mapped to `0..65535` |
| `6` | 2 B | `uint16_t` | `pressure` | Clamped stylus pressure `[0.0, 1.0]` mapped to `0..65535` (`0` when `type == 0` hover) |
| `8` | 1 B | `int8_t` | `tiltX` | Horizontal tilt in degrees, clamped to `[-90, 90]`; positive = pen leans right |
| `9` | 1 B | `int8_t` | `tiltY` | Vertical tilt in degrees, clamped to `[-90, 90]`; positive = pen leans toward the user |
| `10` | 4 B | `uint32_t` | `seq` | Monotonically increasing packet counter for out-of-order/stale packet filtering |
| `14` | 2 B | `uint16_t` | `timestamp` | Sample time in 100 µs ticks, wrapping every 6.5 s; `0` = not provided (total = 16 bytes) |
