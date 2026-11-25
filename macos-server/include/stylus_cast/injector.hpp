#pragma once

#include "stylus_cast/pen_sample.hpp"
#include "stylus_cast/pen_state.hpp"

#include <ApplicationServices/ApplicationServices.h>

namespace stylus_cast {

bool hasAccessibilityPermission(bool prompt);
void printAccessibilityWarning();

// Owns the pen state and posts the resulting events to the window server.
class Injector {
public:
    Injector(CGRect activeRect, bool dryRun);
    ~Injector();
    Injector(const Injector&)            = delete;
    Injector& operator=(const Injector&) = delete;

    void inject(const PenSample& sample);
    void streamLost(); // release the button and leave proximity

    bool isDown() const;
    bool inProximity() const;

private:
    void post(const PenSteps& steps, const PenSample& sample, bool sampleIsCurrent);

    CGRect           activeRect_;
    bool             dryRun_;
    CGEventSourceRef source_ = nullptr;
    PenState         state_;
    PenSample        last_;
    CGPoint          lastPoint_;
};

} // namespace stylus_cast
