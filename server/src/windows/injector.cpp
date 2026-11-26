#include "stylus_cast/injector.hpp"

#include "stylus_cast/tablet_events.hpp"

#include <iostream>

namespace stylus_cast {

void checkPlatformPermissions(const ServerConfig& /*config*/) {}

Injector::Injector(ScreenRect activeRect, ScreenRect monitorBounds, uintptr_t targetMonitor, bool dryRun)
    : activeRect_(activeRect), dryRun_(dryRun), lastPoint_{activeRect.x, activeRect.y} {
    if (dryRun_) {
        return;
    }

    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        createDevice_ = reinterpret_cast<CreateDeviceFn>(
            reinterpret_cast<void*>(::GetProcAddress(user32, "CreateSyntheticPointerDevice")));
        createDevice2_ = reinterpret_cast<CreateDevice2Fn>(
            reinterpret_cast<void*>(::GetProcAddress(user32, "CreateSyntheticPointerDevice2")));
        injectInput_ = reinterpret_cast<InjectInputFn>(
            reinterpret_cast<void*>(::GetProcAddress(user32, "InjectSyntheticPointerInput")));
        destroyDevice_ = reinterpret_cast<DestroyDeviceFn>(
            reinterpret_cast<void*>(::GetProcAddress(user32, "DestroySyntheticPointerDevice")));
    }

    if ((createDevice_ == nullptr && createDevice2_ == nullptr) || injectInput_ == nullptr ||
        destroyDevice_ == nullptr) {
        std::cerr << "[Warning] Windows Synthetic Pointer API unavailable (requires Windows 10 1809+).\n";
        return;
    }

    HMONITOR monitorHandle = reinterpret_cast<HMONITOR>(targetMonitor);
    if (createDevice2_ != nullptr && monitorHandle != nullptr) {
        SyntheticDeviceCreationParams params{};
        params.pointerType  = PT_PEN;
        params.maxCount     = 1;
        params.feedbackMode = POINTER_FEEDBACK_DEFAULT;
        params.hMonitor     = monitorHandle;
        params.deviceWidth  = 0;
        params.deviceHeight = 0;
        params.options      = 0; // SDCO_NONE
        device_             = createDevice2_(&params);
        if (device_ != nullptr) {
            // Coordinates injected with hMonitor set are relative to that monitor's top-left corner.
            activeRect_ = {
                activeRect.x - monitorBounds.x,
                activeRect.y - monitorBounds.y,
                activeRect.width,
                activeRect.height,
            };
            lastPoint_ = {activeRect_.x, activeRect_.y};
        }
    }

    if (device_ == nullptr && createDevice_ != nullptr) {
        device_ = createDevice_(PT_PEN, 1, POINTER_FEEDBACK_DEFAULT);
    }

    if (device_ == nullptr) {
        std::cerr << "[Warning] CreateSyntheticPointerDevice(PT_PEN) failed (error " << ::GetLastError()
                  << "); pen injection disabled.\n";
    }
}

Injector::~Injector() {
    streamLost();
    if (device_ != nullptr && destroyDevice_ != nullptr) {
        destroyDevice_(device_);
        device_ = nullptr;
    }
}

bool Injector::isAvailable() const {
    return dryRun_ || device_ != nullptr;
}

void Injector::inject(const PenSample& sample) {
    post(state_.onSample(sample), sample, true);
    last_ = sample;
}

void Injector::streamLost() {
    post(state_.onStreamLost(), last_, false);
}

bool Injector::isDown() const {
    return state_.isDown();
}

bool Injector::inProximity() const {
    return state_.inProximity();
}

void Injector::post(const PenSteps& steps, const PenSample& sample, bool sampleIsCurrent) {
    const POINT current = sampleIsCurrent ? mapToScreen(sample, activeRect_) : lastPoint_;
    for (const PenStep& step : steps) {
        if (step.action == PenAction::ProximityEnter) {
            pendingNew_ = true;
            continue;
        }

        const bool isNew = pendingNew_;
        pendingNew_      = false;

        if (dryRun_ || device_ == nullptr || injectInput_ == nullptr) {
            continue;
        }

        // A release not caused by an UP packet must not smear the stroke to a new position.
        const bool  implicitRelease = step.action == PenAction::Release && sample.type != PACKET_TYPE_UP;
        const POINT target          = implicitRelease ? lastPoint_ : current;
        const auto  info            = buildPointerInput(step, sample, target, isNew);
        if (!info.has_value()) {
            continue;
        }

        if (!injectInput_(device_, &(*info), 1)) {
            const DWORD err = ::GetLastError();
            if (err == ERROR_ACCESS_DENIED && !warnedAccessDenied_) {
                warnedAccessDenied_ = true;
                std::cerr << "\n[Warning] InjectSyntheticPointerInput returned ERROR_ACCESS_DENIED (5).\n"
                             "          Windows UIPI blocks pointer injection when targeting elevated (Admin)\n"
                             "          windows or while the desktop is locked. Run stylus-cast-server.exe as\n"
                             "          Administrator if you need to draw into elevated applications.\n\n";
            }
        }
    }
    lastPoint_ = current;
}

} // namespace stylus_cast
