#pragma once

#include "stylus_cast/display.hpp"
#include "stylus_cast/pen_sample.hpp"
#include "stylus_cast/pen_state.hpp"

#ifdef _WIN32
#include <windows.h>
#include <optional>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace stylus_cast {

#ifdef _WIN32

// Maps normalized coordinates into `rect` in virtual screen pixel coordinates.
// The far edge maps to the last pixel inside the display rather than one past it.
POINT mapToScreen(const PenSample& sample, const ScreenRect& rect);

// Builds a Windows Ink synthetic pen input frame; returns nullopt for ProximityEnter,
// which is signaled via `isNew` (POINTER_FLAG_NEW) on the immediately following frame.
std::optional<POINTER_TYPE_INFO> buildPointerInput(const PenStep&   step,
                                                   const PenSample& sample,
                                                   POINT            at,
                                                   bool             isNew);

#elif defined(__APPLE__)

// Maps normalized coordinates into `rect` in macOS global logical coordinates.
// The far edge maps to the last point inside the display rather than one past it.
CGPoint mapToScreen(const PenSample& sample, const ScreenRect& rect);

// The caller owns the returned event; may be null.
CGEventRef createEvent(CGEventSourceRef source, const PenStep& step, const PenSample& sample, CGPoint at);

#endif

} // namespace stylus_cast
