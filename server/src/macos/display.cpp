#include "stylus_cast/display.hpp"

#include <ApplicationServices/ApplicationServices.h>

#include <algorithm>
#include <cmath>
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
    for (uint32_t i = 0; i < static_cast<uint32_t>(ids.size()); ++i) {
        const CGDirectDisplayID id     = ids[i];
        const CGRect            cgRect = CGDisplayBounds(id);

        DisplayInfo info;
        info.index        = i;
        info.nativeHandle = static_cast<uintptr_t>(id);
        info.deviceName   = "Display " + std::to_string(id);
        info.bounds       = {
            static_cast<int32_t>(std::lround(cgRect.origin.x)),
            static_cast<int32_t>(std::lround(cgRect.origin.y)),
            static_cast<int32_t>(std::lround(cgRect.size.width)),
            static_cast<int32_t>(std::lround(cgRect.size.height)),
        };
        info.pixelWidth  = static_cast<uint32_t>(CGDisplayPixelsWide(id));
        info.pixelHeight = static_cast<uint32_t>(CGDisplayPixelsHigh(id));
        info.isPrimary   = (id == mainId);
        displays.push_back(std::move(info));
    }
    return displays;
}

DisplayInfo defaultFallbackDisplay() {
    DisplayInfo fallback;
    fallback.bounds      = {0, 0, 1920, 1200};
    fallback.pixelWidth  = 1920;
    fallback.pixelHeight = 1200;
    fallback.isPrimary   = true;
    return fallback;
}

void printDisplays(const std::vector<DisplayInfo>& displays) {
    std::cout << "Active macOS Displays (" << displays.size() << "):\n";
    for (const DisplayInfo& d : displays) {
        const double aspect = d.bounds.height > 0 ? static_cast<double>(d.bounds.width) / d.bounds.height : 1.6;
        std::cout << "  [" << d.index << "] " << d.deviceName << (d.isPrimary ? " (Main)" : "")
                  << " Logical=" << d.bounds.width << "x" << d.bounds.height << " Origin=(" << d.bounds.x << ","
                  << d.bounds.y << ") Pixels=" << d.pixelWidth << "x" << d.pixelHeight << " Aspect=" << std::fixed
                  << std::setprecision(3) << aspect << std::defaultfloat << "\n";
    }
}

} // namespace stylus_cast
