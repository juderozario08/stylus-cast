#include "penbridge/beacon.hpp"
#include "penbridge/config.hpp"
#include "penbridge/display.hpp"
#include "penbridge/injector.hpp"
#include "penbridge/server.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_running{true};

void onSignal(int /*signal*/) {
    g_running.store(false, std::memory_order_relaxed);
}

CGRect selectDisplay(penbridge::ServerConfig& config, const std::vector<penbridge::DisplayInfo>& displays) {
    if (displays.empty()) {
        std::cerr << "[Warning] Could not query active displays; defaulting to 1920x1200 bounds.\n";
        return CGRectMake(0, 0, 1920, 1200);
    }
    if (config.displayIndex >= displays.size()) {
        std::cerr << "Requested display index " << config.displayIndex << " out of range (0.." << displays.size() - 1
                  << "). Using display 0.\n";
        config.displayIndex = 0;
    }
    return displays[config.displayIndex].bounds;
}

void printBanner(const penbridge::ServerConfig& config, const CGRect& rect) {
    const std::string delay =
        config.delayUs < 0 ? "auto" : std::to_string(config.delayUs / 1000) + " ms";
    std::cout << "========================================================================\n"
                 " PenBridge macOS Server Started\n"
                 "========================================================================\n"
              << "  UDP Stylus Port : " << config.udpPort << "\n"
              << "  Discovery Beacon: UDP broadcast :" << config.beaconPort << " (1 Hz)\n"
              << "  Target Display  : [" << config.displayIndex << "] " << rect.size.width << "x" << rect.size.height
              << " at (" << rect.origin.x << "," << rect.origin.y << ")\n"
              << "  Playout Delay   : " << delay << "\n"
              << "  Smoothing       : " << config.smoothing << "/10\n"
              << "  Mode            : " << (config.dryRun ? "DRY-RUN (no OS injection)" : "CoreGraphics HID Event Tap")
              << "\n========================================================================" << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace penbridge;

    ServerConfig config;
    switch (parseArguments(argc, argv, config)) {
        case ParseResult::Help:
            return 0;
        case ParseResult::Error:
            return 1;
        case ParseResult::Run:
            break;
    }

    const auto displays = enumerateDisplays();
    if (config.listDisplays) {
        printDisplays(displays);
        return 0;
    }
    const CGRect rect = computeActiveRect(selectDisplay(config, displays), config.aspect);

    if (!config.dryRun && !hasAccessibilityPermission(config.promptAccessibility)) {
        printAccessibilityWarning();
    }

    Server server(config, rect);
    if (!server.open()) {
        return 1;
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    printBanner(config, rect);

    const Beacon beacon(config.beaconPort, config.verbose);
    server.run(g_running);

    std::cout << "\nShutting down PenBridge server..." << std::endl;
    return 0;
}
