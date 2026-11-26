#include "check.hpp"
#include "stylus_cast/config.hpp"
#include "stylus_cast/display.hpp"
#include "stylus_cast/tablet_events.hpp"

#ifdef __APPLE__
#include <IOKit/hidsystem/IOLLEvent.h>
#endif

namespace stylus_cast::test {

namespace {

#ifdef _WIN32

constexpr POINT AT = {10, 20};

void dragCarriesPressureAndTilt(double pressure, UINT32 expectedPressure) {
    PenSample s;
    s.type     = PACKET_TYPE_MOVE;
    s.pressure = pressure;
    s.tiltX    = 0.5;
    s.tiltY    = 0.5;

    const auto input = buildPointerInput({PenAction::Drag, PenButton::Left, false}, s, AT, false);
    CHECK(input.has_value());
    if (!input.has_value()) {
        return;
    }
    const POINTER_PEN_INFO& pen = input->penInfo;
    CHECK(input->type == PT_PEN && pen.pointerInfo.pointerType == PT_PEN);
    CHECK((pen.pointerInfo.pointerFlags & (POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_UPDATE |
                                           POINTER_FLAG_FIRSTBUTTON)) ==
          (POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_UPDATE | POINTER_FLAG_FIRSTBUTTON));
    CHECK(pen.pressure == expectedPressure);
    CHECK(pen.tiltX == 45 && pen.tiltY == 45);
    CHECK((pen.penMask & (PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y)) ==
          (PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y));
}

#elif defined(__APPLE__)

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
    CHECK(isNear(doubleField(e, kCGMouseEventPressure), pressure));
    CHECK(isNear(doubleField(e, kCGTabletEventPointPressure), pressure));
    CHECK(isNear(doubleField(e, kCGTabletEventTiltX), 0.5));
    CHECK(isNear(doubleField(e, kCGTabletEventTiltY), -0.5));
    CHECK(CGEventGetIntegerValueField(e, kCGMouseEventSubtype) == kCGEventMouseSubtypeTabletPoint);
    CFRelease(e);
}

#endif

} // namespace

void tabletEventTests() {
#ifdef _WIN32
    dragCarriesPressureAndTilt(0.25, 256);
    dragCarriesPressureAndTilt(0.50, 512);
    dragCarriesPressureAndTilt(1.00, 1024);

    // ProximityEnter is merged as POINTER_FLAG_NEW on the immediately following frame.
    PenSample s;
    CHECK(!buildPointerInput({PenAction::ProximityEnter, PenButton::Left, false}, s, AT, false).has_value());

    const auto hoverNew = buildPointerInput({PenAction::Hover, PenButton::Left, false}, s, AT, true);
    CHECK(hoverNew.has_value());
    CHECK((hoverNew->penInfo.pointerInfo.pointerFlags &
           (POINTER_FLAG_NEW | POINTER_FLAG_INRANGE | POINTER_FLAG_UPDATE)) ==
          (POINTER_FLAG_NEW | POINTER_FLAG_INRANGE | POINTER_FLAG_UPDATE));
    CHECK((hoverNew->penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) == 0);

    // Press guarantees non-zero pressure even when sample pressure is 0.
    const auto press = buildPointerInput({PenAction::Press, PenButton::Left, false}, s, AT, false);
    CHECK(press.has_value() && press->penInfo.pressure >= 1);
    CHECK((press->penInfo.pointerInfo.pointerFlags & (POINTER_FLAG_DOWN | POINTER_FLAG_INCONTACT)) ==
          (POINTER_FLAG_DOWN | POINTER_FLAG_INCONTACT));
    CHECK(press->penInfo.pointerInfo.ButtonChangeType == POINTER_CHANGE_FIRSTBUTTON_DOWN);

    // Right button / barrel button and release.
    s.pressure       = 0.9;
    const auto right = buildPointerInput({PenAction::Press, PenButton::Right, false}, s, AT, false);
    CHECK(right.has_value());
    CHECK((right->penInfo.pointerInfo.pointerFlags & POINTER_FLAG_SECONDBUTTON) != 0);
    CHECK((right->penInfo.penFlags & PEN_FLAG_BARREL) != 0);
    CHECK(right->penInfo.pointerInfo.ButtonChangeType == POINTER_CHANGE_SECONDBUTTON_DOWN);

    const auto up = buildPointerInput({PenAction::Release, PenButton::Right, false}, s, AT, false);
    CHECK(up.has_value() && up->penInfo.pressure == 0);
    CHECK((up->penInfo.pointerInfo.pointerFlags & (POINTER_FLAG_INRANGE | POINTER_FLAG_UP)) ==
          (POINTER_FLAG_INRANGE | POINTER_FLAG_UP));
    CHECK(up->penInfo.pointerInfo.ButtonChangeType == POINTER_CHANGE_SECONDBUTTON_UP);

    // Eraser tool flags and proximity leave.
    const auto eraser = buildPointerInput({PenAction::Drag, PenButton::Left, true}, s, AT, false);
    CHECK(eraser.has_value() && (eraser->penInfo.penFlags & (PEN_FLAG_INVERTED | PEN_FLAG_ERASER)) ==
                                    (PEN_FLAG_INVERTED | PEN_FLAG_ERASER));

    const auto leave = buildPointerInput({PenAction::ProximityLeave, PenButton::Left, true}, s, AT, false);
    CHECK(leave.has_value());
    CHECK((leave->penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INRANGE) == 0);
    CHECK((leave->penInfo.penFlags & PEN_FLAG_INVERTED) != 0);

    // Screen mapping and multi-monitor negative coordinates.
    s.x           = 1.0;
    s.y           = 1.0;
    const POINT p = mapToScreen(s, {-1920, 0, 1920, 1080});
    CHECK(p.x == -1 && p.y == 1079);

#elif defined(__APPLE__)
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
    const CGPoint c = mapToScreen(s, {0, 0, 1440, 900});
    CHECK(c.x == 1439.0 && c.y == 899.0);
#endif

    // Shared letterboxing computation.
    const ScreenRect boxed = computeActiveRect({0, 0, 2560, 1440}, 4.0 / 3.0);
    CHECK(boxed.x == 320 && boxed.y == 0 && boxed.width == 1920 && boxed.height == 1440);

    // Shared CLI argument parsing sanity.
    ServerConfig cfg;
    const char*  argv[] = {"stylus-cast-server", "--delay", "12", "--smooth", "6", "--aspect", "16:10",
                           "--usb-only", "--dry-run"};
    CHECK(parseArguments(9, argv, cfg) == ParseResult::Run);
    CHECK(cfg.delayUs == 12'000 && cfg.smoothing == 6 && isNear(cfg.aspect, 1.6) && !cfg.udpEnabled &&
          cfg.usbEnabled && cfg.dryRun);

    ServerConfig badCfg;
    const char*  badArgv[] = {"stylus-cast-server", "--usb-only", "--no-usb"};
    CHECK(parseArguments(3, badArgv, badCfg) == ParseResult::Error);
}

} // namespace stylus_cast::test
