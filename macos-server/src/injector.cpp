#include "penbridge/injector.hpp"

#include "penbridge/tablet_events.hpp"

#include <CoreFoundation/CoreFoundation.h>

#include <iostream>

namespace penbridge {

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
                 "      'penbridge-server' binary directly.\n"
                 "   3. Restart penbridge-server (or pass --prompt-accessibility).\n"
                 "========================================================================\n\n";
}

Injector::Injector(CGRect activeRect, bool dryRun)
    : activeRect_(activeRect), dryRun_(dryRun), lastPoint_(activeRect.origin) {
    if (!dryRun_) {
        source_ = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    }
}

Injector::~Injector() {
    streamLost();
    if (source_ != nullptr) {
        CFRelease(source_);
    }
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

} // namespace penbridge
