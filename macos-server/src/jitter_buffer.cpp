#include "stylus_cast/jitter_buffer.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace stylus_cast {

namespace {

constexpr int64_t TICK_US              = 100;
constexpr int64_t CLOCK_TIMEOUT_US     = 1'500'000; // well inside the 3.2s timestamp ambiguity
constexpr int64_t OFFSET_WINDOW_US     = 4'000'000;
constexpr int64_t MIN_SPACING_US       = 1'000;  // spread catch-up bursts so apps see each point
constexpr int64_t MAX_BACKLOG_US       = 150'000;
constexpr int64_t GAP_THRESHOLD_US     = 6'000;
constexpr int64_t MAX_GAP_US           = 80'000;
constexpr int64_t INTERPOLATED_STEP_US = 4'000;
constexpr int64_t DELAY_MARGIN_US      = 2'000;
constexpr size_t  MIN_ADAPT_SAMPLES    = 64;

int64_t percentile(std::vector<int32_t>& sorted, double p) {
    return sorted[static_cast<size_t>(p * static_cast<double>(sorted.size() - 1))];
}

// Point on the segment p0 -> p1 at t in [0, 1], bent so that it leaves p0 with `startSlope`.
double curve(double p0, double p1, double startSlope, double t) {
    return p0 + startSlope * t + (p1 - p0 - startSlope) * t * t;
}

// Slope at `from` (scaled to the gap) of the parabola through before, from and to.
double startSlope(double before, double from, double to, double beforeGap, double gap) {
    const double incoming = (from - before) / beforeGap;
    const double outgoing = (to - from) / gap;
    return (incoming * gap + outgoing * beforeGap) / (beforeGap + gap) * gap;
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

} // namespace

JitterBuffer::JitterBuffer(int64_t delayUs)
    : auto_(delayUs < 0), delayUs_(delayUs < 0 ? START_AUTO_DELAY : delayUs) {}

void JitterBuffer::reset() {
    queue_.clear();
    haveClock_ = false;
    previous_.reset();
    beforePrevious_.reset();
}

int64_t JitterBuffer::unwrapTime(uint16_t timestamp, int64_t arrivalUs) {
    if (!haveClock_ || arrivalUs - lastArrivalUs_ > CLOCK_TIMEOUT_US) {
        haveClock_     = true;
        sourceUs_      = 0;
        windowStartUs_ = arrivalUs;
        currentMinUs_  = std::numeric_limits<int64_t>::max();
        previousMinUs_ = std::numeric_limits<int64_t>::max();
        previous_.reset(); // times before this point are on a different timeline
        beforePrevious_.reset();
    } else {
        const auto delta = static_cast<int16_t>(static_cast<uint16_t>(timestamp - lastTicks_));
        sourceUs_ += static_cast<int64_t>(delta) * TICK_US;
    }
    lastTicks_     = timestamp;
    lastArrivalUs_ = arrivalUs;
    return sourceUs_;
}

int64_t JitterBuffer::updateOffset(int64_t transitUs, int64_t arrivalUs) {
    if (arrivalUs - windowStartUs_ > OFFSET_WINDOW_US) {
        previousMinUs_ = currentMinUs_;
        currentMinUs_  = transitUs;
        windowStartUs_ = arrivalUs;
    }
    currentMinUs_ = std::min(currentMinUs_, transitUs);
    return std::min(currentMinUs_, previousMinUs_);
}

void JitterBuffer::recordJitter(int64_t jitterUs) {
    jitterUs_[jitterNext_] = static_cast<int32_t>(std::min<int64_t>(jitterUs, 10'000'000));
    jitterNext_            = (jitterNext_ + 1) % JITTER_WINDOW;
    jitterCount_           = std::min(jitterCount_ + 1, JITTER_WINDOW);
}

// Only called between strokes, so the delay is constant while ink is being drawn.
void JitterBuffer::adaptDelay() {
    if (!auto_ || jitterCount_ < MIN_ADAPT_SAMPLES) {
        return;
    }
    std::vector<int32_t> sorted(jitterUs_.begin(), jitterUs_.begin() + static_cast<std::ptrdiff_t>(jitterCount_));
    std::sort(sorted.begin(), sorted.end());
    const int64_t target = std::clamp(percentile(sorted, 0.98) + DELAY_MARGIN_US, MIN_AUTO_DELAY, MAX_AUTO_DELAY);
    // Grow at once, shrink gradually.
    delayUs_ = target > delayUs_ ? target : delayUs_ + (target - delayUs_) / 8;
}

void JitterBuffer::enqueue(const PenSample& sample, int64_t idealUs, int64_t arrivalUs) {
    int64_t due = std::max(idealUs, arrivalUs);
    if (due < lastDueUs_ + MIN_SPACING_US) {
        const int64_t spaced = lastDueUs_ + MIN_SPACING_US;
        due = (spaced - arrivalUs > MAX_BACKLOG_US) ? std::max(due, lastDueUs_) : spaced;
    }
    lastDueUs_ = due;
    queue_.push_back({sample, due});
}

void JitterBuffer::interpolateTo(const PenSample& sample, int64_t offsetUs, int64_t arrivalUs) {
    if (!previous_ || sample.type != PACKET_TYPE_MOVE) {
        return;
    }
    const PenSample& from = *previous_;
    const int64_t    gap  = sample.timeUs - from.timeUs;
    const bool       moved = sample.x != from.x || sample.y != from.y;
    if (!moved || gap <= GAP_THRESHOLD_US || gap > MAX_GAP_US) {
        return;
    }

    double slopeX = sample.x - from.x;
    double slopeY = sample.y - from.y;
    if (beforePrevious_ && from.timeUs > beforePrevious_->timeUs) {
        const auto beforeGap = static_cast<double>(from.timeUs - beforePrevious_->timeUs);
        slopeX = startSlope(beforePrevious_->x, from.x, sample.x, beforeGap, static_cast<double>(gap));
        slopeY = startSlope(beforePrevious_->y, from.y, sample.y, beforeGap, static_cast<double>(gap));
    }

    const int64_t steps = (gap + INTERPOLATED_STEP_US / 2) / INTERPOLATED_STEP_US;
    for (int64_t i = 1; i < steps; ++i) {
        const double t  = static_cast<double>(i) / static_cast<double>(steps);
        PenSample    mid = sample;
        mid.x           = std::clamp(curve(from.x, sample.x, slopeX, t), 0.0, 1.0);
        mid.y           = std::clamp(curve(from.y, sample.y, slopeY, t), 0.0, 1.0);
        mid.pressure    = lerp(from.pressure, sample.pressure, t);
        mid.tiltX       = lerp(from.tiltX, sample.tiltX, t);
        mid.tiltY       = lerp(from.tiltY, sample.tiltY, t);
        mid.timeUs      = from.timeUs + gap * i / steps;
        enqueue(mid, mid.timeUs + offsetUs + delayUs_, arrivalUs);
        ++interpolated_;
    }
}

void JitterBuffer::push(PenSample sample, uint16_t timestamp, int64_t arrivalUs) {
    if (delayUs_ == 0 && !auto_) { // pass-through
        sample.timeUs = arrivalUs;
        lastDueUs_    = arrivalUs;
        queue_.push_back({sample, arrivalUs});
        return;
    }

    int64_t offset = 0;
    if (timestamp == 0) { // client without timestamps: arrival time is the best available
        sample.timeUs = arrivalUs;
    } else {
        sample.timeUs         = unwrapTime(timestamp, arrivalUs);
        const int64_t transit = arrivalUs - sample.timeUs;
        offset                = updateOffset(transit, arrivalUs);
        recordJitter(transit - offset);
    }

    if (sample.type == PACKET_TYPE_DOWN) {
        adaptDelay();
    }

    const int64_t ideal = sample.timeUs + offset + delayUs_;
    if (arrivalUs > ideal) {
        ++late_;
    }

    interpolateTo(sample, offset, arrivalUs);
    enqueue(sample, ideal, arrivalUs);

    if (sample.inContact()) {
        beforePrevious_ = sample.type == PACKET_TYPE_DOWN ? std::nullopt : previous_;
        previous_       = sample;
    } else {
        previous_.reset();
        beforePrevious_.reset();
    }
}

std::optional<PenSample> JitterBuffer::popDue(int64_t nowUs) {
    if (queue_.empty() || queue_.front().dueUs > nowUs) {
        return std::nullopt;
    }
    const PenSample sample = queue_.front().sample;
    queue_.pop_front();
    return sample;
}

std::optional<int64_t> JitterBuffer::nextDueUs() const {
    return queue_.empty() ? std::nullopt : std::optional<int64_t>(queue_.front().dueUs);
}

int64_t JitterBuffer::delayUs() const {
    return delayUs_;
}

uint64_t JitterBuffer::lateCount() const {
    return late_;
}

uint64_t JitterBuffer::interpolatedCount() const {
    return interpolated_;
}

JitterStats JitterBuffer::jitter() const {
    if (jitterCount_ == 0) {
        return {};
    }
    std::vector<int32_t> sorted(jitterUs_.begin(), jitterUs_.begin() + static_cast<std::ptrdiff_t>(jitterCount_));
    std::sort(sorted.begin(), sorted.end());
    const auto ms = [&sorted](double p) { return static_cast<double>(percentile(sorted, p)) / 1000.0; };
    return {ms(0.50), ms(0.95), ms(0.99), ms(1.0)};
}

} // namespace stylus_cast
