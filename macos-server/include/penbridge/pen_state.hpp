#pragma once

#include "penbridge/pen_sample.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace penbridge {

enum class PenAction : uint8_t { ProximityEnter, ProximityLeave, Hover, Press, Drag, Release };
enum class PenButton : uint8_t { Left, Right };

struct PenStep {
    PenAction action = PenAction::Hover;
    PenButton button = PenButton::Left;
    bool      eraser = false;
};

// Fixed-capacity list, so the hot path never allocates.
class PenSteps {
public:
    void           push(PenStep step);
    size_t         size() const;
    const PenStep& operator[](size_t index) const;
    const PenStep* begin() const;
    const PenStep* end() const;

private:
    std::array<PenStep, 6> items_{};
    size_t                 count_ = 0;
};

// Turns samples into a well-formed event sequence: every Press gets exactly one Release, the
// button is latched for the whole stroke, and tool changes are announced with proximity
// events. Tolerates lost DOWN and UP packets.
class PenState {
public:
    PenSteps onSample(const PenSample& sample);
    PenSteps onStreamLost(); // lift the pen and leave proximity

    bool isDown() const;
    bool inProximity() const;

private:
    void release(PenSteps& out);
    void press(PenSteps& out, uint8_t buttons);
    void ensureTool(PenSteps& out, bool eraser);

    bool      down_   = false;
    bool      near_   = false;
    bool      eraser_ = false;
    PenButton button_ = PenButton::Left;
};

} // namespace penbridge
