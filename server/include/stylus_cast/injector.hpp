#pragma once

#include "stylus_cast/config.hpp"
#include "stylus_cast/display.hpp"
#include "stylus_cast/pen_sample.hpp"
#include "stylus_cast/pen_state.hpp"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#endif

#include <cstdint>

namespace stylus_cast {

// Checks and warns about platform-specific injection permissions (e.g., macOS Accessibility).
void checkPlatformPermissions(const ServerConfig& config);

// Owns the pen state and injects native OS tablet/pen events.
class Injector {
public:
    Injector(ScreenRect activeRect, ScreenRect monitorBounds, uintptr_t targetMonitor, bool dryRun);
    ~Injector();
    Injector(const Injector&)            = delete;
    Injector& operator=(const Injector&) = delete;

    bool isAvailable() const;

    void inject(const PenSample& sample);
    void streamLost(); // release the button and leave proximity

    bool isDown() const;
    bool inProximity() const;

private:
    void post(const PenSteps& steps, const PenSample& sample, bool sampleIsCurrent);

    ScreenRect activeRect_;
    bool       dryRun_;
    PenState   state_;
    PenSample  last_;

#ifdef _WIN32
    struct SyntheticDeviceCreationParams {
        POINTER_INPUT_TYPE    pointerType;
        ULONG                 maxCount;
        POINTER_FEEDBACK_MODE feedbackMode;
        HMONITOR              hMonitor;
        ULONG                 deviceWidth;
        ULONG                 deviceHeight;
        UINT32                options;
    };

    using CreateDeviceFn  = HSYNTHETICPOINTERDEVICE(WINAPI*)(POINTER_INPUT_TYPE, ULONG, POINTER_FEEDBACK_MODE);
    using CreateDevice2Fn = HSYNTHETICPOINTERDEVICE(WINAPI*)(const SyntheticDeviceCreationParams*);
    using InjectInputFn   = BOOL(WINAPI*)(HSYNTHETICPOINTERDEVICE, const POINTER_TYPE_INFO*, UINT32);
    using DestroyDeviceFn = VOID(WINAPI*)(HSYNTHETICPOINTERDEVICE);

    CreateDeviceFn          createDevice_       = nullptr;
    CreateDevice2Fn         createDevice2_      = nullptr;
    InjectInputFn           injectInput_        = nullptr;
    DestroyDeviceFn         destroyDevice_      = nullptr;
    HSYNTHETICPOINTERDEVICE device_             = nullptr;
    bool                    warnedAccessDenied_ = false;
    bool                    pendingNew_         = false;
    POINT                   lastPoint_;
#elif defined(__APPLE__)
    CGEventSourceRef source_ = nullptr;
    CGPoint          lastPoint_;
#endif
};

} // namespace stylus_cast
