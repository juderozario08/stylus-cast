#pragma once

#include <cstdint>

namespace penbridge {

// Gives the calling thread real-time scheduling, which exempts it from timer coalescing
// and Low Power Mode throttling. Returns false if the system refused.
bool makeThreadRealtime();

int64_t monotonicMicros();

} // namespace penbridge
