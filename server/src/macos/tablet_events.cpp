#include "stylus_cast/tablet_events.hpp"

#include <IOKit/hidsystem/IOLLEvent.h>

#include <algorithm>
#include <cstdint>

namespace stylus_cast {

namespace {

constexpr int64_t DEVICE_ID         = 1;
constexpr int64_t VENDOR_ID         = 1;
constexpr int64_t TABLET_ID         = 1;
constexpr int64_t SYSTEM_TABLET_ID  = 1;
constexpr int64_t PEN_POINTER_ID    = 1;
constexpr int64_t ERASER_POINTER_ID = 2;

// Smallest value that survives kCGMouseEventPressure's 8-bit storage.
constexpr double MIN_CONTACT_PRESSURE = 1.0 / 255.0;

constexpr int64_t CAPABILITIES = NX_TABLET_CAPABILITY_DEVICEIDMASK | NX_TABLET_CAPABILITY_BUTTONSMASK |
                                 NX_TABLET_CAPABILITY_TILTXMASK | NX_TABLET_CAPABILITY_TILTYMASK |
                                 NX_TABLET_CAPABILITY_PRESSUREMASK;

CGEventType eventType(const PenStep& step) {
    const bool right = step.button == PenButton::Right;
    switch (step.action) {
        case PenAction::Press:
            return right ? kCGEventRightMouseDown : kCGEventLeftMouseDown;
        case PenAction::Drag:
            return right ? kCGEventRightMouseDragged : kCGEventLeftMouseDragged;
        case PenAction::Release:
            return right ? kCGEventRightMouseUp : kCGEventLeftMouseUp;
        default:
            return kCGEventMouseMoved;
    }
}

CGEventRef createProximityEvent(CGEventSourceRef source, bool entering, bool eraser) {
    CGEventRef event = CGEventCreate(source);
    if (event == nullptr) {
        return nullptr;
    }
    CGEventSetType(event, kCGEventTabletProximity);
    CGEventSetIntegerValueField(event, kCGTabletProximityEventVendorID, VENDOR_ID);
    CGEventSetIntegerValueField(event, kCGTabletProximityEventTabletID, TABLET_ID);
    CGEventSetIntegerValueField(event, kCGTabletProximityEventSystemTabletID, SYSTEM_TABLET_ID);
    CGEventSetIntegerValueField(event, kCGTabletProximityEventDeviceID, DEVICE_ID);
    CGEventSetIntegerValueField(event, kCGTabletProximityEventPointerID, eraser ? ERASER_POINTER_ID : PEN_POINTER_ID);
    CGEventSetIntegerValueField(event, kCGTabletProximityEventPointerType,
                                eraser ? NX_TABLET_POINTER_ERASER : NX_TABLET_POINTER_PEN);
    CGEventSetIntegerValueField(event, kCGTabletProximityEventCapabilityMask, CAPABILITIES);
    CGEventSetIntegerValueField(event, kCGTabletProximityEventEnterProximity, entering ? 1 : 0);
    return event;
}

CGEventRef createPointEvent(CGEventSourceRef source, const PenStep& step, const PenSample& sample, CGPoint at) {
    const CGMouseButton button = step.button == PenButton::Right ? kCGMouseButtonRight : kCGMouseButtonLeft;
    CGEventRef          event  = CGEventCreateMouseEvent(source, eventType(step), at, button);
    if (event == nullptr) {
        return nullptr;
    }

    // A pressure of exactly 0 while touching makes apps treat the stroke as lifted.
    const bool   touching = step.action == PenAction::Press || step.action == PenAction::Drag;
    const double pressure = touching ? std::max(MIN_CONTACT_PRESSURE, sample.pressure) : 0.0;

    CGEventSetIntegerValueField(event, kCGMouseEventSubtype, kCGEventMouseSubtypeTabletPoint);
    // NSEvent.pressure reads the *mouse* pressure field (default 1.0), so both must be set.
    CGEventSetDoubleValueField(event, kCGMouseEventPressure, pressure);
    CGEventSetDoubleValueField(event, kCGTabletEventPointPressure, pressure);
    // macOS tilt Y is positive away from the user; the wire format is positive toward the user.
    CGEventSetDoubleValueField(event, kCGTabletEventTiltX, sample.tiltX);
    CGEventSetDoubleValueField(event, kCGTabletEventTiltY, -sample.tiltY);
    CGEventSetIntegerValueField(event, kCGTabletEventDeviceID, DEVICE_ID);

    // Tablet button mask: bit 0 = tip contact, bit 1 = lower barrel button.
    const int64_t tip    = touching ? 0x1 : 0x0;
    const int64_t barrel = (sample.buttons & BUTTON_PRIMARY) != 0 ? 0x2 : 0x0;
    CGEventSetIntegerValueField(event, kCGTabletEventPointButtons, tip | barrel);
    return event;
}

} // namespace

CGPoint mapToScreen(const PenSample& sample, const ScreenRect& rect) {
    const double maxX = static_cast<double>(std::max<int32_t>(0, rect.width - 1));
    const double maxY = static_cast<double>(std::max<int32_t>(0, rect.height - 1));
    const double nx   = std::clamp(sample.x, 0.0, 1.0);
    const double ny   = std::clamp(sample.y, 0.0, 1.0);
    return CGPointMake(static_cast<double>(rect.x) + nx * maxX,
                       static_cast<double>(rect.y) + ny * maxY);
}

CGEventRef createEvent(CGEventSourceRef source, const PenStep& step, const PenSample& sample, CGPoint at) {
    if (step.action == PenAction::ProximityEnter || step.action == PenAction::ProximityLeave) {
        return createProximityEvent(source, step.action == PenAction::ProximityEnter, step.eraser);
    }
    return createPointEvent(source, step, sample, at);
}

} // namespace stylus_cast
