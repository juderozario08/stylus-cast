#pragma once

#include <cstdint>

namespace stylus_cast {

enum class Verdict : uint8_t { Accept, NewSession, Stale, Foreign };

// Decides whether a packet belongs to the active stream. The first client owns the stream
// until it has been silent for SESSION_IDLE_MS, so a second device cannot interleave input.
class StreamFilter {
public:
    static constexpr int64_t SESSION_IDLE_MS = 1500;

    Verdict  check(uint32_t clientAddr, uint32_t seq, int64_t nowMs);
    void     reset(); // release stream ownership immediately (e.g. transport switch/disconnect)
    uint64_t lost() const;
    int64_t  silentMs(int64_t nowMs) const; // 0 before the first packet

private:
    bool     active_  = false;
    uint32_t addr_    = 0;
    uint32_t lastSeq_ = 0;
    int64_t  lastMs_  = 0;
    uint64_t lost_    = 0;
};

} // namespace stylus_cast
