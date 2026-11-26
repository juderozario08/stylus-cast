#include "check.hpp"
#include "stylus_cast/jitter_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace stylus_cast::test {

namespace {

constexpr int64_t STEP_US = 4'000; // 250 Hz pen

uint16_t ticks(int64_t us) {
    const auto t = static_cast<uint16_t>((us / 100) & 0xFFFF);
    return t == 0 ? uint16_t{1} : t;
}

PenSample at(uint8_t type, double x, double y = 0.5) {
    PenSample s;
    s.type = type;
    s.x    = x;
    s.y    = y;
    return s;
}

struct Arrival {
    int64_t sentUs;
    int64_t arrivalUs;
};

// Replays a stroke and returns the time each sample was played at (100us clock).
std::vector<int64_t> play(JitterBuffer& buffer, const std::vector<Arrival>& arrivals) {
    std::vector<int64_t> played;
    size_t               next = 0;
    for (int64_t now = 0; now < arrivals.back().arrivalUs + 500'000; now += 100) {
        while (next < arrivals.size() && arrivals[next].arrivalUs <= now) {
            const uint8_t type = next == 0 ? PACKET_TYPE_DOWN : PACKET_TYPE_MOVE;
            buffer.push(at(type, 0.001 * static_cast<double>(next)), ticks(arrivals[next].sentUs),
                        arrivals[next].arrivalUs);
            ++next;
        }
        while (buffer.popDue(now)) {
            played.push_back(now);
        }
    }
    return played;
}

// Transit 2ms, plus a 20ms network stall every 25 packets that releases them as a burst.
std::vector<Arrival> burstyLink(int count) {
    std::vector<Arrival> arrivals;
    for (int i = 0; i < count; ++i) {
        const int64_t sent     = 100'000 + i * STEP_US;
        const int     phase    = i % 25;
        const int64_t released = (i - phase + 20 + 5) * STEP_US + 100'000;
        const int64_t arrival  = phase >= 20 ? released + 2'000 : sent + 2'000;
        arrivals.push_back({sent, arrival});
    }
    return arrivals;
}

int64_t worstGap(const std::vector<int64_t>& times) {
    int64_t worst = 0;
    for (size_t i = 1; i < times.size(); ++i) {
        worst = std::max(worst, times[i] - times[i - 1]);
    }
    return worst;
}

int64_t arrivalWorstGap(const std::vector<Arrival>& arrivals) {
    int64_t worst = 0;
    for (size_t i = 1; i < arrivals.size(); ++i) {
        worst = std::max(worst, arrivals[i].arrivalUs - arrivals[i - 1].arrivalUs);
    }
    return worst;
}

void absorbsBursts() {
    const auto arrivals = burstyLink(500);
    CHECK(arrivalWorstGap(arrivals) >= 20'000);
    JitterBuffer buffer(30'000);
    const auto   played = play(buffer, arrivals);
    CHECK(played.size() == arrivals.size());
    CHECK(worstGap(played) <= STEP_US + 200); // perfectly even despite 20ms stalls
    CHECK(buffer.lateCount() == 0);
}

void lateSamplesAreSpacedNotDropped() {
    const auto   arrivals = burstyLink(500);
    JitterBuffer buffer(5'000); // too small for this link
    const auto   played = play(buffer, arrivals);
    CHECK(played.size() == arrivals.size());
    CHECK(buffer.lateCount() > 0);
    size_t crowded = 0;
    for (size_t i = 1; i < played.size(); ++i) {
        crowded += (played[i] - played[i - 1] < 900) ? 1 : 0;
    }
    CHECK(crowded == 0);
}

void adaptsBetweenStrokesOnly() {
    JitterBuffer buffer;
    CHECK(buffer.delayUs() == JitterBuffer::START_AUTO_DELAY);
    int64_t now = 100'000;
    buffer.push(at(PACKET_TYPE_DOWN, 0.1), ticks(now), now);
    for (int i = 0; i < 400; ++i) {
        now += STEP_US;
        buffer.push(at(PACKET_TYPE_MOVE, 0.1), ticks(now), now + (i % 10 == 0 ? 30'000 : 0));
    }
    CHECK(buffer.delayUs() == JitterBuffer::START_AUTO_DELAY); // constant during the stroke
    now += 100'000;
    buffer.push(at(PACKET_TYPE_UP, 0.1), ticks(now), now);
    buffer.push(at(PACKET_TYPE_DOWN, 0.1), ticks(now), now);
    CHECK(buffer.delayUs() >= 30'000 && buffer.delayUs() <= JitterBuffer::MAX_AUTO_DELAY);
}

void timestampWraps() {
    JitterBuffer         buffer(10'000);
    std::vector<Arrival> arrivals;
    for (int i = 0; i < 4000; ++i) { // 16s of 250Hz crosses the 6.5s wrap twice
        const int64_t sent = 100'000 + i * STEP_US;
        arrivals.push_back({sent, sent + 2'000});
    }
    const auto played = play(buffer, arrivals);
    CHECK(played.size() == arrivals.size() && worstGap(played) <= STEP_US + 300);
}

void fillsGapsWithACurve() {
    JitterBuffer buffer(10'000);
    const auto   circle = [](double angle) {
        return at(PACKET_TYPE_MOVE, 0.5 + 0.1 * std::cos(angle), 0.5 + 0.1 * std::sin(angle));
    };
    int64_t                now  = 100'000;
    const double           step = 0.15;
    std::vector<PenSample> out;
    for (int i = 0; i < 40; ++i, now += STEP_US) {
        if (i % 8 >= 5) {
            continue; // 3 consecutive packets lost
        }
        PenSample s = circle(step * i);
        if (i == 0) {
            s.type = PACKET_TYPE_DOWN;
        }
        buffer.push(s, ticks(now), now);
    }
    while (const auto s = buffer.popDue(now + 1'000'000)) {
        out.push_back(*s);
    }
    CHECK(buffer.interpolatedCount() > 0);
    double worst = 0.0;
    for (const PenSample& s : out) {
        worst = std::max(worst, std::fabs(std::hypot(s.x - 0.5, s.y - 0.5) - 0.1));
    }
    // A straight chord across the 0.6 rad gap would be off by 0.1 * (1 - cos(0.3)) = 0.0045.
    CHECK(worst < 0.0030);
}

void passThroughAndLegacy() {
    JitterBuffer off(0);
    off.push(at(PACKET_TYPE_MOVE, 0.5), ticks(5'000), 10'000);
    CHECK(off.popDue(10'000).has_value());

    JitterBuffer legacy(10'000); // client without timestamps
    legacy.push(at(PACKET_TYPE_MOVE, 0.5), 0, 10'000);
    CHECK(!legacy.popDue(19'000).has_value() && legacy.popDue(20'000).has_value());

    legacy.push(at(PACKET_TYPE_MOVE, 0.5), 0, 30'000);
    legacy.reset();
    CHECK(!legacy.nextDueUs().has_value());
}

} // namespace

void jitterBufferTests() {
    absorbsBursts();
    lateSamplesAreSpacedNotDropped();
    adaptsBetweenStrokesOnly();
    timestampWraps();
    fillsGapsWithACurve();
    passThroughAndLegacy();
}

} // namespace stylus_cast::test
