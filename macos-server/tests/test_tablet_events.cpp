#include "check.hpp"
#include "penbridge/tablet_events.hpp"

#include <IOKit/hidsystem/IOLLEvent.h>

namespace penbridge::test {

namespace {

constexpr CGPoint AT = {10, 10};

double doubleField(CGEventRef event, CGEventField field) {
    return CGEventGetDoubleValueField(event, field);
}

void dragCarriesPressureAndTilt(double pressure) {
    PenSample s;
    s.type     = PACKET_TYPE_MOVE;
    s.pressure = pressure;
    s.tiltX    = 0.5;
    s.tiltY    = 0.5;
    CGEventRef e = createEvent(nullptr, {PenAction::Drag, PenButton::Left, false}, s, AT);
    CHECK(e != nullptr);
    if (e == nullptr) {
        return;
    }
    CHECK(CGEventGetType(e) == kCGEventLeftMouseDragged);
    CHECK(near(doubleField(e, kCGMouseEventPressure), pressure));
    CHECK(near(doubleField(e, kCGTabletEventPointPressure), pressure));
    CHECK(near(doubleField(e, kCGTabletEventTiltX), 0.5));
    CHECK(near(doubleField(e, kCGTabletEventTiltY), -0.5));
    CHECK(CGEventGetIntegerValueField(e, kCGMouseEventSubtype) == kCGEventMouseSubtypeTabletPoint);
    CFRelease(e);
}

} // namespace

void tabletEventTests() {
    dragCarriesPressureAndTilt(0.1);
    dragCarriesPressureAndTilt(0.5);
    dragCarriesPressureAndTilt(0.9);

    PenSample s;
    CGEventRef press = createEvent(nullptr, {PenAction::Press, PenButton::Left, false}, s, AT);
    CHECK(press != nullptr && doubleField(press, kCGMouseEventPressure) > 0.0);

    s.pressure    = 0.9;
    CGEventRef up = createEvent(nullptr, {PenAction::Release, PenButton::Right, false}, s, AT);
    CHECK(up != nullptr && CGEventGetType(up) == kCGEventRightMouseUp);
    CHECK(up != nullptr && doubleField(up, kCGMouseEventPressure) == 0.0);

    CGEventRef prox = createEvent(nullptr, {PenAction::ProximityEnter, PenButton::Left, true}, s, AT);
    CHECK(prox != nullptr && CGEventGetType(prox) == kCGEventTabletProximity);
    CHECK(prox != nullptr &&
          CGEventGetIntegerValueField(prox, kCGTabletProximityEventPointerType) == NX_TABLET_POINTER_ERASER);
    CHECK(prox != nullptr && CGEventGetIntegerValueField(prox, kCGTabletProximityEventEnterProximity) == 1);

    for (CGEventRef e : {press, up, prox}) {
        if (e != nullptr) {
            CFRelease(e);
        }
    }

    s.x             = 1.0;
    s.y             = 1.0;
    const CGPoint c = mapToScreen(s, CGRectMake(0, 0, 1440, 900));
    CHECK(c.x == 1439.0 && c.y == 899.0);
}

} // namespace penbridge::test
