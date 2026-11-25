#pragma once

#include "penbridge/protocol.hpp"

#include <cstdint>

namespace penbridge {

// One pen sample in normalized units, at full precision.
struct PenSample {
    uint8_t type     = PACKET_TYPE_HOVER;
    uint8_t buttons  = 0;
    double  x        = 0.0; // [0, 1]
    double  y        = 0.0; // [0, 1]
    double  pressure = 0.0; // [0, 1]
    double  tiltX    = 0.0; // [-1, 1], positive = leans right
    double  tiltY    = 0.0; // [-1, 1], positive = leans toward the user
    int64_t timeUs   = 0;   // sample time, see JitterBuffer

    bool inContact() const;
};

PenSample toSample(const StylusPacket& packet);

} // namespace penbridge
