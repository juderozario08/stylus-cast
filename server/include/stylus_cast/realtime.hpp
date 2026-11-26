#pragma once

#include <atomic>
#include <cstdint>

namespace stylus_cast {

// One-time OS initialization/cleanup (DPI awareness & Winsock on Windows; SIGPIPE ignore on macOS).
bool platformInit();
void platformCleanup();
void installSignalHandlers(std::atomic<bool>& running);

// Raises process/thread scheduling priority for even playout timing.
bool makeThreadRealtime();

// Restores timer resolution or scheduling settings modified by makeThreadRealtime().
void endThreadRealtime();

int64_t monotonicMicros();

} // namespace stylus_cast
