#include "stylus_cast/config.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace stylus_cast {

namespace {

constexpr unsigned long MAX_DELAY_MS = 200;

bool parseIpv4(const char* text, uint32_t& outAddr) {
    in_addr address{};
#ifdef _WIN32
    if (::InetPtonA(AF_INET, text, &address) != 1 || address.s_addr == 0) {
        return false;
    }
#else
    if (::inet_pton(AF_INET, text, &address) != 1 || address.s_addr == 0) {
        return false;
    }
#endif
    outAddr = address.s_addr;
    return true;
}

void printUsage(const char* program) {
#ifdef _WIN32
    const char* platformTitle = "Stylus-Cast Windows Digitizer Server";
    const char* modeDesc      = "Windows Ink events";
    const char* displayWord   = "primary display";
#else
    const char* platformTitle = "Stylus-Cast macOS Digitizer Server";
    const char* modeDesc      = "CGEvents";
    const char* displayWord   = "main display";
#endif
    std::cout << platformTitle << "\n\n"
              << "Usage: " << program << " [options]\n\n"
              << "Options:\n"
                 "  --delay <ms|auto>      Constant playout delay that hides Wi-Fi jitter; 0 = off (default: auto)\n"
                 "  --smooth <0-10>        Stroke smoothing; 0 = off (default: 4)\n"
                 "  --port <port>          UDP and USB/TCP port for 16-byte stylus packets (default: 9000)\n"
                 "  --beacon-port <port>   UDP broadcast port for 1Hz discovery beacon (default: 9001)\n"
                 "  --display <index>      Target display index (default: 0 = " << displayWord << ")\n"
                 "  --list-displays        List active displays and exit\n"
                 "  --aspect <W:H>         Letterbox target display mapping to W:H aspect ratio (e.g., 16:10)\n"
                 "  --client <ip>          Only accept Wi-Fi UDP packets from this IPv4 address\n"
                 "  --usb-only             Disable Wi-Fi UDP & beacon; accept USB/TCP connections only\n"
                 "  --no-usb               Disable USB/TCP listener and automatic ADB port forwarding\n"
                 "  --stats                Print packet rate, jitter and loss every 2 seconds\n"
                 "  --trace <file>         Record input and output timing to a CSV file for diagnosis\n"
                 "  --verbose, -v          Log stylus Down/Up transitions, USB, and discovery events\n"
#ifdef __APPLE__
                 "  --prompt-accessibility Prompt macOS System Settings if Accessibility is not granted\n"
#endif
                 "  --dry-run              Receive and validate packets without injecting " << modeDesc << "\n"
                 "  --help, -h             Show this help text\n";
}

bool parseUnsigned(const std::string& text, unsigned long max, unsigned long& out) {
    if (text.empty() || text[0] == '-') {
        return false;
    }
    char*               end   = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || value > max) {
        return false;
    }
    out = value;
    return true;
}

bool parsePositiveDouble(const std::string& text, double& out) {
    if (text.empty()) {
        return false;
    }
    char*        end   = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(value) || value <= 0.0) {
        return false;
    }
    out = value;
    return true;
}

bool parseAspect(const std::string& text, double& out) {
    const size_t colon = text.find(':');
    if (colon == std::string::npos) {
        return parsePositiveDouble(text, out);
    }
    double w = 0.0;
    double h = 0.0;
    if (!parsePositiveDouble(text.substr(0, colon), w) || !parsePositiveDouble(text.substr(colon + 1), h)) {
        return false;
    }
    out = w / h;
    return true;
}

bool parseFlag(const std::string& name, ServerConfig& config) {
    if (name == "--list-displays") {
        config.listDisplays = true;
    } else if (name == "--stats") {
        config.showStats = true;
    } else if (name == "--verbose" || name == "-v") {
        config.verbose = true;
    } else if (name == "--dry-run") {
        config.dryRun = true;
    } else if (name == "--prompt-accessibility") {
        config.promptAccessibility = true;
    } else if (name == "--usb-only") {
        config.udpEnabled = false;
    } else if (name == "--no-usb") {
        config.usbEnabled = false;
    } else {
        return false;
    }
    return true;
}

// Returns empty on success, or what the option expected.
std::string applyValue(const std::string& name, const std::string& value, ServerConfig& config) {
    unsigned long number = 0;
    if (name == "--port") {
        if (!parseUnsigned(value, 65535, number) || number == 0) {
            return "a port between 1 and 65535";
        }
        config.udpPort = static_cast<uint16_t>(number);
    } else if (name == "--beacon-port") {
        if (!parseUnsigned(value, 65535, number) || number == 0) {
            return "a port between 1 and 65535";
        }
        config.beaconPort = static_cast<uint16_t>(number);
    } else if (name == "--display") {
        if (!parseUnsigned(value, 100, number)) {
            return "a display index";
        }
        config.displayIndex = static_cast<uint32_t>(number);
    } else if (name == "--delay") {
        if (value == "auto") {
            config.delayUs = JitterBuffer::AUTO_DELAY;
        } else if (parseUnsigned(value, MAX_DELAY_MS, number)) {
            config.delayUs = static_cast<int64_t>(number) * 1000;
        } else {
            return "'auto' or 0..200";
        }
    } else if (name == "--smooth") {
        if (!parseUnsigned(value, StrokeSmoother::MAX_LEVEL, number)) {
            return "0..10";
        }
        config.smoothing = static_cast<int>(number);
    } else if (name == "--aspect") {
        if (!parseAspect(value, config.aspect)) {
            return "W:H (e.g., 16:10)";
        }
    } else if (name == "--client") {
        if (!parseIpv4(value.c_str(), config.allowedClient)) {
            return "an IPv4 address";
        }
    } else if (name == "--trace") {
        if (value.empty()) {
            return "a file path";
        }
        config.tracePath = value;
    } else {
        return "unknown";
    }
    return {};
}

} // namespace

ParseResult parseArguments(int argc, const char* const* argv, ServerConfig& config) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return ParseResult::Help;
        }
        if (parseFlag(arg, config)) {
            continue;
        }
        if (i + 1 >= argc) {
            std::cerr << "Missing value for " << arg << " (or unknown option). Run with --help.\n";
            return ParseResult::Error;
        }
        const std::string expected = applyValue(arg, argv[++i], config);
        if (expected == "unknown") {
            std::cerr << "Unknown argument: " << arg << ". Run with --help.\n";
            return ParseResult::Error;
        }
        if (!expected.empty()) {
            std::cerr << "Invalid " << arg << " value '" << argv[i] << "'; expected " << expected << ".\n";
            return ParseResult::Error;
        }
    }
    if (!config.udpEnabled && !config.usbEnabled) {
        std::cerr << "Cannot combine --usb-only and --no-usb; at least one transport must be enabled.\n";
        return ParseResult::Error;
    }
    return ParseResult::Run;
}

} // namespace stylus_cast
