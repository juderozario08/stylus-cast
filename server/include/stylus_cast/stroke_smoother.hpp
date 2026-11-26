#pragma once

#include "stylus_cast/pen_sample.hpp"

#include <cstdint>

namespace stylus_cast {

// Adapts cutoff to speed so slow strokes are steadied without dragging behind fast ones.
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

class StrokeSmoother {
public:
    static constexpr int MAX_LEVEL     = 10;
    static constexpr int DEFAULT_LEVEL = 4;

    // level 0 disables smoothing; width/height are the target area in screen points/pixels.
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
