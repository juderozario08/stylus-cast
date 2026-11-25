#pragma once

#include <ApplicationServices/ApplicationServices.h>

#include <cstddef>
#include <vector>

namespace stylus_cast {

struct DisplayInfo {
    CGDirectDisplayID id          = 0;
    CGRect            bounds      = {};
    size_t            pixelWidth  = 0;
    size_t            pixelHeight = 0;
    bool              isMain      = false;
};

std::vector<DisplayInfo> enumerateDisplays(); // main display first
void                     printDisplays(const std::vector<DisplayInfo>& displays);

// Area of the display the tablet maps to, letterboxed to `aspect` if it is > 0.
CGRect computeActiveRect(CGRect bounds, double aspect);

} // namespace stylus_cast
