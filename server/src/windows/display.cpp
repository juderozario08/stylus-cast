#include "stylus_cast/display.hpp"

#include <windows.h>

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace stylus_cast {

namespace {

std::string wideToUtf8(const wchar_t* text) {
    if (text == nullptr || text[0] == L'\0') {
        return {};
    }
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(len - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), len, nullptr, nullptr);
    return out;
}

BOOL CALLBACK monitorEnumProc(HMONITOR hMonitor, HDC /*hdc*/, LPRECT /*lprc*/, LPARAM dwData) {
    auto* displays = reinterpret_cast<std::vector<DisplayInfo>*>(dwData);

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(hMonitor, &mi)) {
        return TRUE;
    }

    const int32_t width  = mi.rcMonitor.right - mi.rcMonitor.left;
    const int32_t height = mi.rcMonitor.bottom - mi.rcMonitor.top;

    uint32_t pixelW = width > 0 ? static_cast<uint32_t>(width) : 0;
    uint32_t pixelH = height > 0 ? static_cast<uint32_t>(height) : 0;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (::EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
        pixelW = dm.dmPelsWidth;
        pixelH = dm.dmPelsHeight;
    }

    DisplayInfo info;
    info.nativeHandle = reinterpret_cast<uintptr_t>(hMonitor);
    info.deviceName   = wideToUtf8(mi.szDevice);
    info.bounds       = {mi.rcMonitor.left, mi.rcMonitor.top, width, height};
    info.pixelWidth   = pixelW;
    info.pixelHeight  = pixelH;
    info.isPrimary    = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    displays->push_back(std::move(info));
    return TRUE;
}

} // namespace

std::vector<DisplayInfo> enumerateDisplays() {
    std::vector<DisplayInfo> displays;
    ::EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc, reinterpret_cast<LPARAM>(&displays));
    std::stable_partition(displays.begin(), displays.end(), [](const DisplayInfo& d) { return d.isPrimary; });
    for (uint32_t i = 0; i < static_cast<uint32_t>(displays.size()); ++i) {
        displays[i].index = i;
    }
    return displays;
}

DisplayInfo defaultFallbackDisplay() {
    DisplayInfo fallback;
    fallback.bounds       = {0, 0, 1920, 1080};
    fallback.pixelWidth   = 1920;
    fallback.pixelHeight  = 1080;
    fallback.isPrimary    = true;
    fallback.nativeHandle = reinterpret_cast<uintptr_t>(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    return fallback;
}

void printDisplays(const std::vector<DisplayInfo>& displays) {
    std::cout << "Active Windows Displays (" << displays.size() << "):\n";
    for (const DisplayInfo& d : displays) {
        const double aspect = d.bounds.height > 0 ? static_cast<double>(d.bounds.width) / d.bounds.height : 1.6;
        std::cout << "  [" << d.index << "] " << d.deviceName << (d.isPrimary ? " (Primary)" : "")
                  << " Bounds=" << d.bounds.width << "x" << d.bounds.height << " Origin=(" << d.bounds.x << ","
                  << d.bounds.y << ") Pixels=" << d.pixelWidth << "x" << d.pixelHeight << " Aspect=" << std::fixed
                  << std::setprecision(3) << aspect << std::defaultfloat << "\n";
    }
}

} // namespace stylus_cast
