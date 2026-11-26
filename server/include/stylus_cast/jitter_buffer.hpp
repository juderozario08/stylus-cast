#pragma once

#include "stylus_cast/pen_sample.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace stylus_cast {

struct JitterStats {
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double maxMs = 0.0;
};

// Trades a small constant delay for even timing. Samples are replayed at the spacing the
// pen produced them with, `delay` after the fastest observed transit, so network stalls and
// bursts shorter than the delay are invisible. Gaps in a stroke (lost packets, slow
// digitizer) are filled with a curve instead of a straight segment.
class JitterBuffer {
public:
    static constexpr int64_t AUTO_DELAY       = -1;
    static constexpr int64_t MIN_AUTO_DELAY   = 6'000;
    static constexpr int64_t MAX_AUTO_DELAY   = 45'000;
    static constexpr int64_t START_AUTO_DELAY = 20'000;

    // delayUs: AUTO_DELAY adapts between strokes; 0 disables buffering; otherwise fixed.
    explicit JitterBuffer(int64_t delayUs = AUTO_DELAY);

    void push(PenSample sample, uint16_t timestamp, int64_t arrivalUs);

    // Next sample whose play time has come, with timeUs set to its pen timeline position.
    std::optional<PenSample> popDue(int64_t nowUs);
    std::optional<int64_t>   nextDueUs() const;

    void reset(); // forget the clock mapping and drop queued samples

    int64_t     delayUs() const;
    uint64_t    lateCount() const; // samples that arrived after their play time
    uint64_t    interpolatedCount() const;
    JitterStats jitter() const;    // over the most recent samples

private:
    struct Queued {
        PenSample sample;
        int64_t   dueUs;
    };

    int64_t unwrapTime(uint16_t timestamp, int64_t arrivalUs);
    int64_t updateOffset(int64_t transitUs, int64_t arrivalUs);
    void    recordJitter(int64_t jitterUs);
    void    adaptDelay();
    void    enqueue(const PenSample& sample, int64_t idealUs, int64_t arrivalUs);
    void    interpolateTo(const PenSample& sample, int64_t offsetUs, int64_t arrivalUs);

    static constexpr size_t JITTER_WINDOW = 1024;

    const bool auto_;
    int64_t    delayUs_;

    std::deque<Queued> queue_;
    int64_t            lastDueUs_ = 0;

    bool     haveClock_     = false;
    uint16_t lastTicks_     = 0;
    int64_t  sourceUs_      = 0;
    int64_t  lastArrivalUs_ = 0;

    // Minimum transit over the current and previous window; tracks clock drift.
    int64_t windowStartUs_ = 0;
    int64_t currentMinUs_  = 0;
    int64_t previousMinUs_ = 0;

    std::array<int32_t, JITTER_WINDOW> jitterUs_{};
    size_t                             jitterNext_  = 0;
    size_t                             jitterCount_ = 0;

    std::optional<PenSample> previous_;       // last real contact sample
    std::optional<PenSample> beforePrevious_;

    uint64_t late_         = 0;
    uint64_t interpolated_ = 0;
};

} // namespace stylus_cast
