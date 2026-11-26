#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace stylus_cast {

// Automatically configures `adb reverse tcp:<port> tcp:<port>` when an Android
// device is attached over USB with USB Debugging enabled, allowing zero-loss
// USB streaming without Wi-Fi or Mobile Hotspot.
class AdbBridge {
public:
    AdbBridge(uint16_t port, bool verbose, const std::atomic<bool>& usbClientConnected);
    ~AdbBridge();
    AdbBridge(const AdbBridge&)            = delete;
    AdbBridge& operator=(const AdbBridge&) = delete;

private:
    void run();

    uint16_t                 port_;
    bool                     verbose_;
    const std::atomic<bool>& usbClientConnected_;
    std::atomic<bool>        running_{true};
    std::thread              thread_;
};

} // namespace stylus_cast
