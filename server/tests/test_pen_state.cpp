#include "check.hpp"
#include "stylus_cast/pen_state.hpp"

#include <initializer_list>

namespace stylus_cast::test {

namespace {

using A = PenAction;

PenSample sample(uint8_t type, uint8_t buttons = 0) {
    PenSample s;
    s.type    = type;
    s.buttons = buttons;
    return s;
}

bool same(const PenSteps& steps, std::initializer_list<PenAction> want) {
    if (steps.size() != want.size()) {
        return false;
    }
    size_t i = 0;
    for (const PenAction action : want) {
        if (steps[i++].action != action) {
            return false;
        }
    }
    return true;
}

} // namespace

void penStateTests() {
    {
        PenState s;
        CHECK(same(s.onSample(sample(0)), {A::ProximityEnter, A::Hover}));
        CHECK(same(s.onSample(sample(1)), {A::Press}));
        CHECK(same(s.onSample(sample(2)), {A::Drag}));
        CHECK(same(s.onSample(sample(3)), {A::Release}));
        CHECK(same(s.onSample(sample(3)), {})); // duplicate UP is harmless
    }
    { // lost DOWN, lost UP
        PenState s;
        CHECK(same(s.onSample(sample(2)), {A::ProximityEnter, A::Press}));
        CHECK(same(s.onSample(sample(1)), {A::Release, A::Press}));
        CHECK(same(s.onSample(sample(0)), {A::Release, A::Hover}));
    }
    { // link lost mid-stroke, then resumes
        PenState s;
        s.onSample(sample(1));
        CHECK(same(s.onStreamLost(), {A::Release, A::ProximityLeave}));
        CHECK(!s.isDown() && !s.inProximity());
        CHECK(same(s.onStreamLost(), {}));
        CHECK(same(s.onSample(sample(2)), {A::ProximityEnter, A::Press}));
    }
    { // barrel button latched for the whole stroke
        PenState s;
        s.onSample(sample(0));
        CHECK(s.onSample(sample(1, BUTTON_PRIMARY))[0].button == PenButton::Right);
        CHECK(s.onSample(sample(2))[0].button == PenButton::Right);
        CHECK(s.onSample(sample(3))[0].button == PenButton::Right);
        CHECK(s.onSample(sample(1))[0].button == PenButton::Left);
    }
    { // switching to the eraser re-announces the tool
        PenState s;
        s.onSample(sample(0));
        const PenSteps e = s.onSample(sample(0, BUTTON_ERASER));
        CHECK(same(e, {A::ProximityLeave, A::ProximityEnter, A::Hover}));
        CHECK(!e[0].eraser && e[1].eraser);
    }
}

} // namespace stylus_cast::test
