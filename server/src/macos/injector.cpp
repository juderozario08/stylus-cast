#include "stylus_cast/injector.hpp"

#include "stylus_cast/tablet_events.hpp"

#include <CoreFoundation/CoreFoundation.h>

#include <iostream>

namespace stylus_cast {

namespace {

bool hasAccessibilityPermission(bool prompt) {
    const void*     keys[]   = {kAXTrustedCheckOptionPrompt};
    const void*     values[] = {prompt ? kCFBooleanTrue : kCFBooleanFalse};
    CFDictionaryRef options  = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
    const bool trusted = AXIsProcessTrustedWithOptions(options);
    if (options != nullptr) {
        CFRelease(options);
    }
    return trusted;
}

void printAccessibilityWarning() {
    std::cerr << "\n========================================================================\n"
                 " [!] WARNING: macOS Accessibility Permission NOT Granted\n"
                 "========================================================================\n"
                 " The pen cannot move the cursor until you grant it:\n"
                 "   1. Open System Settings -> Privacy & Security -> Accessibility\n"
                 "   2. Enable your terminal app (Terminal / iTerm2 / VS Code) or the\n"
                 "      'stylus-cast-server' binary directly.\n"
                 "   3. Restart stylus-cast-server (or pass --prompt-accessibility).\n"
                 "========================================================================\n\n";
}

} // namespace

void checkPlatformPermissions(const ServerConfig& config) {
    if (!config.dryRun && !hasAccessibilityPermission(config.promptAccessibility)) {
        printAccessibilityWarning();
    }
}

Injector::Injector(ScreenRect activeRect, ScreenRect /*monitorBounds*/, uintptr_t /*targetMonitor*/, bool dryRun)
    : activeRect_(activeRect),
      dryRun_(dryRun),
      lastPoint_(CGPointMake(static_cast<double>(activeRect.x), static_cast<double>(activeRect.y))) {
    if (!dryRun_) {
        source_ = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
        if (source_ != nullptr) {
            // Avoid macOS's default 0.25s local event suppression window after synthetic posts.
            CGEventSourceSetLocalEventsSuppressionInterval(source_, 0.0);
        } else {
            std::cerr << "[Warning] CGEventSourceCreate failed; pen injection may be degraded.\n";
        }
    }
}

Injector::~Injector() {
    streamLost();
    if (source_ != nullptr) {
        CFRelease(source_);
        source_ = nullptr;
    }
}

bool Injector::isAvailable() const {
    return dryRun_ || source_ != nullptr;
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
    const CGPoint current = sampleIsCurrent ? mapToScreen(sample, activeRect_) : lastPoint_;
    if (!dryRun_) {
        for (const PenStep& step : steps) {
            // A release not caused by an UP packet must not smear the stroke to a new position.
            const bool implicitRelease = step.action == PenAction::Release && sample.type != PACKET_TYPE_UP;
            CGEventRef event           = createEvent(source_, step, sample, implicitRelease ? lastPoint_ : current);
            if (event != nullptr) {
                CGEventPost(kCGHIDEventTap, event);
                CFRelease(event);
            }
        }
    }
    lastPoint_ = current;
}

} // namespace stylus_cast
