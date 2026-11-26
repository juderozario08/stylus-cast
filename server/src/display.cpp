#include "stylus_cast/display.hpp"

#include <algorithm>
#include <cmath>

namespace stylus_cast {

ScreenRect computeActiveRect(ScreenRect bounds, double aspect) {
    if (aspect <= 0.0 || bounds.width <= 0 || bounds.height <= 0) {
        return bounds;
    }
    double width  = static_cast<double>(bounds.width);
    double height = static_cast<double>(bounds.height);
    if (width / height > aspect) {
        width = height * aspect;
    } else {
        height = width / aspect;
    }
    const int32_t activeW = std::max<int32_t>(1, static_cast<int32_t>(std::lround(width)));
    const int32_t activeH = std::max<int32_t>(1, static_cast<int32_t>(std::lround(height)));
    const int32_t offsetX = (bounds.width - activeW) / 2;
    const int32_t offsetY = (bounds.height - activeH) / 2;
    return {bounds.x + offsetX, bounds.y + offsetY, activeW, activeH};
}

} // namespace stylus_cast
