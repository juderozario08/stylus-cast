#pragma once

#include "stylus_cast/pen_sample.hpp"
#include "stylus_cast/pen_state.hpp"

#include <ApplicationServices/ApplicationServices.h>

namespace stylus_cast {

// Maps normalized coordinates into `rect`. The far edge maps to the last point inside the
// display rather than one past it, which belongs to the neighbouring display.
CGPoint mapToScreen(const PenSample& sample, const CGRect& rect);

// The caller owns the returned event; may be null.
CGEventRef createEvent(CGEventSourceRef source, const PenStep& step, const PenSample& sample, CGPoint at);

} // namespace stylus_cast
