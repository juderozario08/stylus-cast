#include "stylus_cast/adb_bridge.hpp"
#include "stylus_cast/beacon.hpp"
#include "stylus_cast/config.hpp"
#include "stylus_cast/display.hpp"
#include "stylus_cast/injector.hpp"
#include "stylus_cast/realtime.hpp"
#include "stylus_cast/server.hpp"

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_running{true};

stylus_cast::DisplayInfo selectDisplay(stylus_cast::ServerConfig&                    config,
                                       const std::vector<stylus_cast::DisplayInfo>& displays) {
    if (displays.empty()) {
        std::cerr << "[Warning] Could not query active displays; using fallback bounds.\n";
        return stylus_cast::defaultFallbackDisplay();
    }
    if (config.displayIndex >= displays.size()) {
        std::cerr << "Requested display index " << config.displayIndex << " out of range (0.." << displays.size() - 1
                  << "). Using display 0.\n";
        config.displayIndex = 0;
    }
    return displays[config.displayIndex];
}

void printBanner(const stylus_cast::ServerConfig& config, const stylus_cast::ScreenRect& rect) {
    const std::string delay = config.delayUs < 0 ? "auto" : std::to_string(config.delayUs / 1000) + " ms";
#ifdef _WIN32
    const char* title       = " Stylus-Cast Windows Server Started\n";
    const char* nativeMode  = "Windows Ink Synthetic Pen (PT_PEN)";
#else
    const char* title       = " Stylus-Cast macOS Server Started\n";
    const char* nativeMode  = "CoreGraphics HID Event Tap";
#endif

    std::cout << "========================================================================\n"
              << title
              << "========================================================================\n"
              << "  UDP Stylus Port : "
              << (config.udpEnabled ? std::to_string(config.udpPort) : "disabled (--usb-only)") << "\n"
              << "  USB (ADB / TCP) : "
              << (config.usbEnabled ? ("port " + std::to_string(config.udpPort) + " (auto adb reverse)")
                                    : "disabled (--no-usb)")
              << "\n"
              << "  Discovery Beacon: "
              << (config.udpEnabled ? ("UDP broadcast :" + std::to_string(config.beaconPort) + " (1 Hz)")
                                    : "disabled (--usb-only)")
              << "\n"
              << "  Target Display  : [" << config.displayIndex << "] " << rect.width << "x" << rect.height << " at ("
              << rect.x << "," << rect.y << ")\n"
              << "  Playout Delay   : " << delay << "\n"
              << "  Smoothing       : " << config.smoothing << "/10\n"
              << "  Mode            : " << (config.dryRun ? "DRY-RUN (no OS injection)" : nativeMode)
              << "\n========================================================================" << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace stylus_cast;

    if (!platformInit()) {
        std::cerr << "Failed to initialize platform networking.\n";
        return 1;
    }

    ServerConfig config;
    switch (parseArguments(argc, argv, config)) {
        case ParseResult::Help:
            platformCleanup();
            return 0;
        case ParseResult::Error:
            platformCleanup();
            return 1;
        case ParseResult::Run:
            break;
    }

    const auto displays = enumerateDisplays();
    if (config.listDisplays) {
        printDisplays(displays);
        platformCleanup();
        return 0;
    }

    const DisplayInfo display = selectDisplay(config, displays);
    const ScreenRect  rect    = computeActiveRect(display.bounds, config.aspect);

    checkPlatformPermissions(config);

    Server server(config, rect, display.bounds, display.nativeHandle);
    if (!server.open()) {
        platformCleanup();
        return 1;
    }

    installSignalHandlers(g_running);
    printBanner(config, rect);

    std::unique_ptr<Beacon> beacon;
    if (config.udpEnabled) {
        beacon = std::make_unique<Beacon>(config.beaconPort, config.verbose);
    }

    std::unique_ptr<AdbBridge> adbBridge;
    if (config.usbEnabled) {
        adbBridge = std::make_unique<AdbBridge>(config.udpPort, config.verbose, server.usbClientConnected());
    }

    server.run(g_running);

    adbBridge.reset();
    beacon.reset();
    std::cout << "\nShutting down Stylus-Cast server..." << std::endl;
    platformCleanup();
    return 0;
}
