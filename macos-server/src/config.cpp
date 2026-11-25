#include "penbridge/config.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace penbridge {

namespace {

constexpr unsigned long MAX_DELAY_MS = 200;

void printUsage(const char* program) {
    std::cout << "PenBridge macOS Digitizer Server\n\n"
              << "Usage: " << program << " [options]\n\n"
              << "Options:\n"
                 "  --delay <ms|auto>      Constant playout delay that hides Wi-Fi jitter; 0 = off (default: auto)\n"
                 "  --smooth <0-10>        Stroke smoothing; 0 = off (default: 4)\n"
                 "  --port <port>          UDP listening port for 16-byte stylus packets (default: 9000)\n"
                 "  --beacon-port <port>   UDP broadcast port for 1Hz discovery beacon (default: 9001)\n"
                 "  --display <index>      Target display index (default: 0 = main display)\n"
                 "  --list-displays        List active displays and exit\n"
                 "  --aspect <W:H>         Letterbox target display mapping to W:H aspect ratio (e.g., 16:10)\n"
                 "  --client <ip>          Only accept stylus packets from this IPv4 address\n"
                 "  --stats                Print packet rate, jitter and loss every 2 seconds\n"
                 "  --trace <file>         Record input and output timing to a CSV file for diagnosis\n"
                 "  --verbose, -v          Log stylus Down/Up transitions and discovery events\n"
                 "  --prompt-accessibility Prompt macOS System Settings if Accessibility is not granted\n"
                 "  --dry-run              Receive and validate packets without injecting CGEvents\n"
                 "  --help, -h             Show this help text\n";
}

bool parsePositiveDouble(const std::string& text, double& out) {
    char*        end   = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || *end != '\0' || !std::isfinite(value) || value <= 0.0) {
        return false;
    }
    out = value;
    return true;
}

// Digits only, within [minValue, maxValue].
bool parseUnsigned(const std::string& text, unsigned long minValue, unsigned long maxValue, unsigned long& out) {
    if (text.empty() || text.size() > 9 || text.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    const unsigned long value = std::strtoul(text.c_str(), nullptr, 10);
    if (value < minValue || value > maxValue) {
        return false;
    }
    out = value;
    return true;
}

bool parseAspect(const std::string& text, double& out) {
    const auto colon = text.find(':');
    if (colon == std::string::npos) {
        return parsePositiveDouble(text, out);
    }
    double width  = 0.0;
    double height = 0.0;
    if (!parsePositiveDouble(text.substr(0, colon), width) || !parsePositiveDouble(text.substr(colon + 1), height)) {
        return false;
    }
    out = width / height;
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
    } else {
        return false;
    }
    return true;
}

bool takesValue(const std::string& name) {
    return name == "--port" || name == "--beacon-port" || name == "--display" || name == "--aspect" ||
           name == "--client" || name == "--delay" || name == "--smooth" || name == "--trace";
}

// Returns a description of the expected value, or an empty string on success.
std::string applyValue(const std::string& name, const std::string& value, ServerConfig& config) {
    unsigned long number = 0;
    if (name == "--port" || name == "--beacon-port") {
        if (!parseUnsigned(value, 1, 65535, number)) {
            return "1..65535";
        }
        (name == "--port" ? config.udpPort : config.beaconPort) = static_cast<uint16_t>(number);
    } else if (name == "--display") {
        if (!parseUnsigned(value, 0, 999, number)) {
            return "a display index";
        }
        config.displayIndex = static_cast<uint32_t>(number);
    } else if (name == "--aspect") {
        if (!parseAspect(value, config.aspect)) {
            return "W:H (e.g., 16:10)";
        }
    } else if (name == "--client") {
        in_addr address{};
        if (::inet_pton(AF_INET, value.c_str(), &address) != 1 || address.s_addr == 0) {
            return "an IPv4 address";
        }
        config.allowedClient = address.s_addr;
    } else if (name == "--delay") {
        if (value == "auto") {
            config.delayUs = JitterBuffer::AUTO_DELAY;
        } else if (parseUnsigned(value, 0, MAX_DELAY_MS, number)) {
            config.delayUs = static_cast<int64_t>(number) * 1000;
        } else {
            return "auto or 0..200";
        }
    } else if (name == "--smooth") {
        if (!parseUnsigned(value, 0, StrokeSmoother::MAX_LEVEL, number)) {
            return "0..10";
        }
        config.smoothing = static_cast<int>(number);
    } else {
        if (value.empty()) {
            return "a file path";
        }
        config.tracePath = value;
    }
    return {};
}

} // namespace

ParseResult parseArguments(int argc, const char* const* argv, ServerConfig& config) {
    for (int i = 1; i < argc; ++i) {
        const std::string name = argv[i];
        if (name == "--help" || name == "-h") {
            printUsage(argv[0]);
            return ParseResult::Help;
        }
        if (parseFlag(name, config)) {
            continue;
        }
        if (!takesValue(name)) {
            std::cerr << "Unknown option: " << name << " (see --help)\n";
            return ParseResult::Error;
        }
        if (i + 1 >= argc) {
            std::cerr << "Missing value for " << name << ".\n";
            return ParseResult::Error;
        }
        const std::string value    = argv[++i];
        const std::string expected = applyValue(name, value, config);
        if (!expected.empty()) {
            std::cerr << "Invalid " << name << " value '" << value << "'. Expected " << expected << ".\n";
            return ParseResult::Error;
        }
    }
    return ParseResult::Run;
}

} // namespace penbridge
