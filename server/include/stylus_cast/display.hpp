#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace stylus_cast {

struct ScreenRect {
    int32_t x      = 0;
    int32_t y      = 0;
    int32_t width  = 0;
    int32_t height = 0;
};

struct DisplayInfo {
    uint32_t    index        = 0;
    uintptr_t   nativeHandle = 0; // HMONITOR on Windows, CGDirectDisplayID on macOS
    std::string deviceName;
    ScreenRect  bounds      = {};
    uint32_t    pixelWidth  = 0;
    uint32_t    pixelHeight = 0;
    bool        isPrimary   = false;
};

std::vector<DisplayInfo> enumerateDisplays(); // primary/main display first
DisplayInfo              defaultFallbackDisplay();
void                     printDisplays(const std::vector<DisplayInfo>& displays);

// Area of the display the tablet maps to, letterboxed to `aspect` if it is > 0.
ScreenRect computeActiveRect(ScreenRect bounds, double aspect);

} // namespace stylus_cast
