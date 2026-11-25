#pragma once

#include "stylus_cast/pen_sample.hpp"

#include <cstdint>

namespace stylus_cast {

// One Euro filter: smooths strongly when the pen is slow (where hand tremor and sensor noise
// show) and barely at all when it is fast (where lag would show).
class OneEuroFilter {
public:
    OneEuroFilter(double minCutoffHz, double beta);

    double filter(double value, double dtSeconds);
    void   reset();

private:
    double minCutoffHz_;
    double beta_;
    bool   primed_     = false;
    double value_      = 0.0;
    double derivative_ = 0.0;
};

// Smooths samples within a stroke. The first and last points of a stroke are never moved.
class StrokeSmoother {
public:
    static constexpr int MAX_LEVEL     = 10;
    static constexpr int DEFAULT_LEVEL = 4;

    // level 0 disables smoothing; width/height are the target area in screen points.
    StrokeSmoother(int level, double width, double height);

    PenSample apply(const PenSample& sample);
    void      reset();

private:
    bool          enabled_;
    double        width_;
    double        height_;
    OneEuroFilter x_;
    OneEuroFilter y_;
    OneEuroFilter pressure_;
    bool          active_     = false;
    int64_t       lastTimeUs_ = 0;
};

} // namespace stylus_cast
