#include "stylus_cast/display.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace stylus_cast {

std::vector<DisplayInfo> enumerateDisplays() {
    uint32_t count = 0;
    if (CGGetActiveDisplayList(0, nullptr, &count) != kCGErrorSuccess || count == 0) {
        return {};
    }
    std::vector<CGDirectDisplayID> ids(count);
    if (CGGetActiveDisplayList(count, ids.data(), &count) != kCGErrorSuccess) {
        return {};
    }
    ids.resize(count);

    const CGDirectDisplayID mainId = CGMainDisplayID();
    std::stable_partition(ids.begin(), ids.end(), [mainId](CGDirectDisplayID id) { return id == mainId; });

    std::vector<DisplayInfo> displays;
    displays.reserve(ids.size());
    for (const CGDirectDisplayID id : ids) {
        displays.push_back(
            {id, CGDisplayBounds(id), CGDisplayPixelsWide(id), CGDisplayPixelsHigh(id), id == mainId});
    }
    return displays;
}

void printDisplays(const std::vector<DisplayInfo>& displays) {
    std::cout << "Active macOS Displays (" << displays.size() << "):\n";
    for (size_t i = 0; i < displays.size(); ++i) {
        const DisplayInfo& d      = displays[i];
        const double       aspect = d.bounds.size.height > 0 ? d.bounds.size.width / d.bounds.size.height : 1.6;
        std::cout << "  [" << i << "] ID=" << d.id << (d.isMain ? " (Main)" : "") << " Logical=" << d.bounds.size.width
                  << "x" << d.bounds.size.height << " Origin=(" << d.bounds.origin.x << "," << d.bounds.origin.y << ")"
                  << " Pixels=" << d.pixelWidth << "x" << d.pixelHeight << " Aspect=" << std::fixed
                  << std::setprecision(3) << aspect << std::defaultfloat << "\n";
    }
}

CGRect computeActiveRect(CGRect bounds, double aspect) {
    if (aspect <= 0.0 || bounds.size.height <= 0.0) {
        return bounds;
    }
    double width  = bounds.size.width;
    double height = bounds.size.height;
    if (width / height > aspect) {
        width = height * aspect;
    } else {
        height = width / aspect;
    }
    return CGRectMake(bounds.origin.x + (bounds.size.width - width) * 0.5,
                      bounds.origin.y + (bounds.size.height - height) * 0.5, width, height);
}

} // namespace stylus_cast
