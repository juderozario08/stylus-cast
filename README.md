# Stylus-Cast: Ultra-Low-Latency Samsung S-Pen to macOS & Windows Digitizer

Stylus-Cast turns a Samsung Galaxy phone with an S-Pen into an offline, zero-loss USB or low-latency wireless absolute-coordinate Wacom-style drawing and handwriting tablet for macOS and Windows.

- **Zero-Loss USB Mode (No Hotspot Required):** Plug in a USB cable with USB Debugging enabled. The server automatically sets up `adb reverse` and receives stylus packets over a `TCP_NODELAY` USB stream (`Lost: 0`) with no Wi-Fi or Mobile Hotspot needed.
- **Direct Wireless Fallback (5 GHz Mobile Hotspot):** When unplugged, uses a direct **5 GHz Mobile Hotspot** (or LAN) with 16-byte little-endian UDP datagrams and 1 Hz discovery beacons.
- **240Hz–360Hz S-Pen Capture:** Requests unbuffered stylus dispatch (`requestUnbufferedDispatch(SOURCE_STYLUS)`), rejects palm and finger touches (`TOOL_TYPE_STYLUS` / `TOOL_TYPE_ERASER`), unrolls every historical `MotionEvent` sample (`0 until event.historySize`), and streams packets from a zero-allocation lock-free ring buffer.
- **Even Strokes & Unified Server Pipeline:** A single cross-platform `server/` codebase runs the exact same jitter buffer, parabolic gap interpolation, One Euro stroke smoother, and press/drag/release state machine on both macOS and Windows. Tune with `--delay` and `--smooth`.
- **Native Stylus Input:**
  - **macOS:** Injects CoreGraphics `CGEvent` events with `kCGEventMouseSubtypeTabletPoint`, `kCGMouseEventPressure`, `kCGTabletEventPointPressure`, `kCGTabletEventTiltX`, and `kCGTabletEventTiltY` at `kCGHIDEventTap` (with zero local event suppression delay), plus tablet proximity events so apps can tell the pen tip from the eraser.
  - **Windows:** Injects native Windows Ink `PT_PEN` pointer events (`CreateSyntheticPointerDevice2` / `InjectSyntheticPointerInput`, Windows 10 1809+) with 1024-level pressure, `tiltX`/`tiltY`, hover, barrel button right-click/right-drag, and inverted eraser flags. Holding the barrel button when the pen touches down makes that stroke a right-click/right-drag.

---

## Repository Structure

```text
.
├── server/
│   ├── CMakeLists.txt           # Unified macOS & Windows CMake build
│   ├── Makefile                 # Thin cross-platform wrapper around CMake
│   ├── include/stylus_cast/     # Public headers
│   ├── src/
│   │   ├── main.cpp             # Unified entry point
│   │   ├── config.cpp           # Unified command-line options
│   │   ├── server.cpp           # Unified UDP + USB/TCP event loop and statistics
│   │   ├── adb_bridge.cpp       # Automatic `adb reverse` setup for USB mode
│   │   ├── protocol.cpp         # 16-byte wire packet decoding and sequencing
│   │   ├── pen_sample.cpp       # Full-precision sample type
│   │   ├── stream_filter.cpp    # Session ownership, stale filtering, loss counting
│   │   ├── jitter_buffer.cpp    # Even-timing playout, adaptive delay, gap filling
│   │   ├── stroke_smoother.cpp  # One Euro stroke smoothing
│   │   ├── pen_state.cpp        # Press/drag/release state machine
│   │   ├── display.cpp          # Shared letterboxing calculation
│   │   ├── trace.cpp            # Optional CSV recording
│   │   ├── macos/               # macOS CoreGraphics, Mach RT, kqueue/ifaddrs backend
│   │   └── windows/             # Windows Ink PT_PEN, DPI/Monitor, IP Helper, QPC backend
│   └── tests/                   # Cross-platform unit & transport tests
├── android-client/
│   ├── gradlew, gradle/         # Gradle 8.14.3 wrapper
│   ├── settings.gradle.kts
│   ├── build.gradle.kts
│   ├── gradle.properties
│   └── app/
│       ├── build.gradle.kts
│       └── src/main/
│           ├── AndroidManifest.xml
│           └── java/com/styluscast/client/
│               ├── StylusPacket.kt    # 16-byte zero-allocation binary encoder & planar tilt math
│               ├── PacketSender.kt    # Lock-free SPSC ring buffer + USB TCP & Wi-Fi UDP sender
│               ├── BeaconDiscovery.kt # UDP :9001 listener for "MAC_TABLET_SERVER" broadcast beacon
│               ├── DigitizerView.kt   # Full-screen canvas, historical sample unrolling & palm rejection
│               └── MainActivity.kt    # Immersive fullscreen, USB monitoring & settings dialog
└── README.md
```

---

## Step-by-Step Setup Guide

### 1. Build & Run the Server (`server/`)

#### macOS
```bash
cd server
make
make test
./stylus-cast-server --stats
```
Or directly with CMake:
```bash
cd server
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/stylus-cast-server --stats
```

**macOS Accessibility Permission:** CoreGraphics `CGEventPost(kCGHIDEventTap, ...)` requires Accessibility authorization in **System Settings > Privacy & Security > Accessibility** (for your terminal app or `stylus-cast-server`). Run `./stylus-cast-server --prompt-accessibility` to trigger the prompt automatically.

#### Windows (Windows 10 1809+ / Windows 11)
With `make` (uses Ninja + `g++` by default):
```powershell
cd server
make
make test
.\stylus-cast-server.exe --stats
```
Or directly with CMake (MinGW or Visual Studio Developer PowerShell):
```powershell
cd server
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
.\build\stylus-cast-server.exe --stats
```

**Windows Notes:**
- **Windows Defender Firewall:** For wireless mode, allow `stylus-cast-server.exe` on Private/Public networks for UDP ports `9000` and `9001`. USB mode uses local loopback via ADB and does not need firewall rules or Wi-Fi.
- **Elevated Apps (UIPI):** Normal desktop apps work without elevation. Run `stylus-cast-server.exe` as Administrator only if drawing into an elevated window.

#### CLI Options
```text
stylus-cast-server [options]

  --delay <ms|auto>      Constant playout delay that hides Wi-Fi jitter; 0 = off (default: auto)
  --smooth <0-10>        Stroke smoothing; 0 = off (default: 4)
  --port <port>          UDP and USB/TCP port for 16-byte stylus packets (default: 9000)
  --beacon-port <port>   UDP broadcast port for 1Hz discovery beacon (default: 9001)
  --display <index>      Target display index (default: 0 = primary/main display)
  --list-displays        List active displays and exit
  --aspect <W:H>         Letterbox target display mapping to W:H aspect ratio (e.g., 16:10)
  --client <ip>          Only accept Wi-Fi UDP packets from this IPv4 address
  --usb-only             Disable Wi-Fi UDP & beacon; accept USB/TCP connections only
  --no-usb               Disable USB/TCP listener and automatic ADB port forwarding
  --stats                Print packet rate, jitter and loss every 2 seconds
  --trace <file>         Record input and output timing to a CSV file for diagnosis
  --verbose, -v          Log stylus Down/Up transitions, USB, and discovery events
  --prompt-accessibility (macOS) Prompt System Settings if Accessibility is not granted
  --dry-run              Receive and validate packets without injecting OS events
```

### 2. Build & Install the Android Client (`android-client/`)
1. From the repository root, build the APK into `dist/`:
   ```bash
   make debug    # outputs dist/stylus-cast-debug.apk
   make release  # outputs dist/stylus-cast-release.apk (or stylus-cast-release-unsigned.apk)
   ```
   *(Or open `android-client/` in **Android Studio** / run `./gradlew assembleDebug` with JDK 17–21 and Android SDK platform 34.)*
2. Connect your Samsung Galaxy phone over USB with **USB Debugging** enabled and install the app (`adb install -r dist/stylus-cast-debug.apk`).

### 3. Connect Over USB (Recommended — Zero Packet Loss, No Hotspot Needed)
1. Plug your Samsung Galaxy phone into your Mac or Windows PC via USB (with **USB Debugging** enabled; Wi-Fi and Mobile Hotspot can be turned **OFF**).
2. Start `stylus-cast-server` on your computer. The server automatically runs `adb reverse tcp:9000 tcp:9000` and prints:
   ```text
   [USB] ADB reverse active on tcp:9000 (ready for USB cable)
   ```
   *(If `adb` is not in your `PATH` or standard Android SDK location, you can run `adb reverse tcp:9000 tcp:9000` manually.)*
3. Launch **Stylus-Cast** on your phone. Within 1 second, the top-right status pill turns green and displays `Connected: USB`.
4. To force USB-only mode (disabling Wi-Fi scanning/locks completely), tap the top-right pill with your finger and choose **USB only (no Wi-Fi / hotspot needed)**, and/or start the server with `--usb-only`.

### 4. Connect Wirelessly (5 GHz Mobile Hotspot)
1. On your Samsung Galaxy phone, open **Settings > Connections > Mobile Hotspot and Tethering > Mobile Hotspot**, choose **5 GHz** band, and connect your computer to the hotspot.
2. Start `stylus-cast-server` and launch **Stylus-Cast** on the phone. Within 1 second, the status pill turns green and displays `Connected: <Host IP>`.
3. Tap the top-right pill with your **finger** to switch transport mode, enter a manual IP, or change the canvas aspect ratio (`Full`, `16:10`, `16:9`, `3:2`, `21:9`).

---

## Binary Wire Protocol Reference (16 Bytes, Little-Endian)

| Offset | Size | Type | Field | Description & Encoding |
| --- | --- | --- | --- | --- |
| `0` | 1 B | `uint8_t` | `type` | `0` = Hover (`ACTION_HOVER_MOVE`), `1` = Down (`ACTION_DOWN`), `2` = Move (`ACTION_MOVE` / historical), `3` = Up/Cancel (`ACTION_UP` / `ACTION_CANCEL`) |
| `1` | 1 B | `uint8_t` | `buttons` | Bit `0` (`0x01`): S-Pen primary barrel button (`BUTTON_STYLUS_PRIMARY`). Bit `1` (`0x02`): Eraser tool (`TOOL_TYPE_ERASER`, or secondary stylus button). |
| `2` | 2 B | `uint16_t` | `x` | Normalized horizontal coordinate inside the active drawing area mapped to `0..65535` |
| `4` | 2 B | `uint16_t` | `y` | Normalized vertical coordinate inside the active drawing area mapped to `0..65535` |
| `6` | 2 B | `uint16_t` | `pressure` | Clamped stylus pressure `[0.0, 1.0]` mapped to `0..65535` (`0` when `type == 0` hover) |
| `8` | 1 B | `int8_t` | `tiltX` | Horizontal tilt in degrees, clamped to `[-90, 90]`; positive = pen leans right |
| `9` | 1 B | `int8_t` | `tiltY` | Vertical tilt in degrees, clamped to `[-90, 90]`; positive = pen leans toward the user |
| `10` | 4 B | `uint32_t` | `seq` | Monotonically increasing packet counter for out-of-order/stale packet filtering |
| `14` | 2 B | `uint16_t` | `timestamp` | Sample time in 100 µs ticks, wrapping every 6.5 s; `0` = not provided (total = 16 bytes) |
