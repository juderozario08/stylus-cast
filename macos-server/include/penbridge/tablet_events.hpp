#pragma once

#include "penbridge/pen_sample.hpp"
#include "penbridge/pen_state.hpp"

#include <ApplicationServices/ApplicationServices.h>

namespace penbridge {

// Maps normalized coordinates into `rect`. The far edge maps to the last point inside the
// display rather than one past it, which belongs to the neighbouring display.
CGPoint mapToScreen(const PenSample& sample, const CGRect& rect);

// The caller owns the returned event; may be null.
CGEventRef createEvent(CGEventSourceRef source, const PenStep& step, const PenSample& sample, CGPoint at);

} // namespace penbridge
