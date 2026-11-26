#include "stylus_cast/tablet_events.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace stylus_cast {

namespace {

constexpr double MAX_PRESSURE        = 1024.0;
constexpr double MIN_CONTACT_SAMPLES = 1.0 / MAX_PRESSURE;

UINT32 scalePressure(double normalized) {
    const double clamped = std::clamp(normalized, MIN_CONTACT_SAMPLES, 1.0);
    return std::clamp<UINT32>(static_cast<UINT32>(std::lround(clamped * MAX_PRESSURE)), 1u, 1024u);
}

INT32 scaleTilt(double normalized) {
    return std::clamp<INT32>(static_cast<INT32>(std::lround(std::clamp(normalized, -1.0, 1.0) * 90.0)), -90, 90);
}

} // namespace

POINT mapToScreen(const PenSample& sample, const ScreenRect& rect) {
    const int32_t maxX = std::max<int32_t>(0, rect.width - 1);
    const int32_t maxY = std::max<int32_t>(0, rect.height - 1);
    const double  nx   = std::clamp(sample.x, 0.0, 1.0);
    const double  ny   = std::clamp(sample.y, 0.0, 1.0);
    return POINT{
        rect.x + static_cast<LONG>(std::lround(nx * static_cast<double>(maxX))),
        rect.y + static_cast<LONG>(std::lround(ny * static_cast<double>(maxY))),
    };
}

std::optional<POINTER_TYPE_INFO> buildPointerInput(const PenStep&   step,
                                                   const PenSample& sample,
                                                   POINT            at,
                                                   bool             isNew) {
    if (step.action == PenAction::ProximityEnter) {
        return std::nullopt;
    }

    POINTER_TYPE_INFO info{};
    info.type                                 = PT_PEN;
    info.penInfo.pointerInfo.pointerType      = PT_PEN;
    info.penInfo.pointerInfo.pointerId        = 0;
    info.penInfo.pointerInfo.ptPixelLocation  = at;
    info.penInfo.pointerInfo.dwTime           = 0;
    info.penInfo.pointerInfo.PerformanceCount = 0;

    const bool barrelHeld = (sample.buttons & BUTTON_PRIMARY) != 0;
    const bool right      = step.button == PenButton::Right;

    switch (step.action) {
        case PenAction::ProximityLeave:
            info.penInfo.pointerInfo.pointerFlags     = POINTER_FLAG_UPDATE | POINTER_FLAG_CONFIDENCE;
            info.penInfo.pointerInfo.ButtonChangeType = POINTER_CHANGE_NONE;
            info.penInfo.penFlags                     = step.eraser ? PEN_FLAG_INVERTED : PEN_FLAG_NONE;
            info.penInfo.penMask                      = PEN_MASK_NONE;
            info.penInfo.pressure                     = 0;
            return info;

        case PenAction::Hover:
            info.penInfo.pointerInfo.pointerFlags =
                (isNew ? POINTER_FLAG_NEW : POINTER_FLAG_NONE) | POINTER_FLAG_INRANGE | POINTER_FLAG_CONFIDENCE |
                POINTER_FLAG_UPDATE;
            info.penInfo.pointerInfo.ButtonChangeType = POINTER_CHANGE_NONE;
            info.penInfo.penFlags =
                (step.eraser ? PEN_FLAG_INVERTED : PEN_FLAG_NONE) | (barrelHeld ? PEN_FLAG_BARREL : PEN_FLAG_NONE);
            info.penInfo.penMask  = PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
            info.penInfo.pressure = 0;
            info.penInfo.tiltX    = scaleTilt(sample.tiltX);
            info.penInfo.tiltY    = scaleTilt(sample.tiltY);
            return info;

        case PenAction::Press:
            info.penInfo.pointerInfo.pointerFlags =
                (isNew ? POINTER_FLAG_NEW : POINTER_FLAG_NONE) | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT |
                POINTER_FLAG_CONFIDENCE | POINTER_FLAG_DOWN |
                (right ? POINTER_FLAG_SECONDBUTTON : POINTER_FLAG_FIRSTBUTTON);
            info.penInfo.pointerInfo.ButtonChangeType =
                right ? POINTER_CHANGE_SECONDBUTTON_DOWN : POINTER_CHANGE_FIRSTBUTTON_DOWN;
            info.penInfo.penFlags = (step.eraser ? (PEN_FLAG_INVERTED | PEN_FLAG_ERASER) : PEN_FLAG_NONE) |
                                    ((right || barrelHeld) ? PEN_FLAG_BARREL : PEN_FLAG_NONE);
            info.penInfo.penMask  = PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
            info.penInfo.pressure = scalePressure(sample.pressure);
            info.penInfo.tiltX    = scaleTilt(sample.tiltX);
            info.penInfo.tiltY    = scaleTilt(sample.tiltY);
            return info;

        case PenAction::Drag:
            info.penInfo.pointerInfo.pointerFlags =
                POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_CONFIDENCE | POINTER_FLAG_UPDATE |
                (right ? POINTER_FLAG_SECONDBUTTON : POINTER_FLAG_FIRSTBUTTON);
            info.penInfo.pointerInfo.ButtonChangeType = POINTER_CHANGE_NONE;
            info.penInfo.penFlags = (step.eraser ? (PEN_FLAG_INVERTED | PEN_FLAG_ERASER) : PEN_FLAG_NONE) |
                                    ((right || barrelHeld) ? PEN_FLAG_BARREL : PEN_FLAG_NONE);
            info.penInfo.penMask  = PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
            info.penInfo.pressure = scalePressure(sample.pressure);
            info.penInfo.tiltX    = scaleTilt(sample.tiltX);
            info.penInfo.tiltY    = scaleTilt(sample.tiltY);
            return info;

        case PenAction::Release:
            info.penInfo.pointerInfo.pointerFlags = POINTER_FLAG_INRANGE | POINTER_FLAG_CONFIDENCE | POINTER_FLAG_UP;
            info.penInfo.pointerInfo.ButtonChangeType =
                right ? POINTER_CHANGE_SECONDBUTTON_UP : POINTER_CHANGE_FIRSTBUTTON_UP;
            info.penInfo.penFlags = step.eraser ? PEN_FLAG_INVERTED : PEN_FLAG_NONE;
            info.penInfo.penMask  = PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
            info.penInfo.pressure = 0;
            info.penInfo.tiltX    = scaleTilt(sample.tiltX);
            info.penInfo.tiltY    = scaleTilt(sample.tiltY);
            return info;

        case PenAction::ProximityEnter:
            break;
    }
    return std::nullopt;
}

} // namespace stylus_cast
