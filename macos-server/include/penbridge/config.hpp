#pragma once

#include "penbridge/jitter_buffer.hpp"
#include "penbridge/protocol.hpp"
#include "penbridge/stroke_smoother.hpp"

#include <cstdint>
#include <string>

namespace penbridge {

struct ServerConfig {
    uint16_t    udpPort       = DEFAULT_UDP_PORT;
    uint16_t    beaconPort    = DEFAULT_BEACON_PORT;
    uint32_t    displayIndex  = 0;
    double      aspect        = 0.0; // 0 = native display aspect ratio
    uint32_t    allowedClient = 0;   // network-order IPv4; 0 = any
    int64_t     delayUs       = JitterBuffer::AUTO_DELAY;
    int         smoothing     = StrokeSmoother::DEFAULT_LEVEL;
    std::string tracePath;
    bool        listDisplays        = false;
    bool        showStats           = false;
    bool        verbose             = false;
    bool        dryRun              = false;
    bool        promptAccessibility = false;
};

enum class ParseResult : uint8_t { Run, Help, Error };

// Prints a message to stderr on Error and the usage text on Help.
ParseResult parseArguments(int argc, const char* const* argv, ServerConfig& config);

} // namespace penbridge
