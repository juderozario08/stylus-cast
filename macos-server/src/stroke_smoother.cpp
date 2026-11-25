#include "stylus_cast/stroke_smoother.hpp"

#include <algorithm>
#include <cmath>

namespace stylus_cast {

namespace {

constexpr double  PI                   = 3.14159265358979323846;
constexpr double  DERIVATIVE_CUTOFF_HZ = 10.0;
constexpr double  PRESSURE_CUTOFF_HZ   = 12.0;
constexpr int64_t MAX_STEP_US          = 200'000;

// Level 1 (light) .. 10 (heavy)
double positionCutoff(int level) {
    return 24.0 / static_cast<double>(std::max(level, 1));
}

// How quickly smoothing backs off with speed: Hz per (screen point / second)
double positionBeta(int level) {
    return 0.24 / static_cast<double>(std::max(level, 1));
}

double smoothingFactor(double cutoffHz, double dt) {
    const double r = 2.0 * PI * cutoffHz * dt;
    return r / (r + 1.0);
}

} // namespace

OneEuroFilter::OneEuroFilter(double minCutoffHz, double beta) : minCutoffHz_(minCutoffHz), beta_(beta) {}

double OneEuroFilter::filter(double value, double dtSeconds) {
    if (!primed_ || dtSeconds <= 0.0) {
        if (!primed_) {
            primed_     = true;
            value_      = value;
            derivative_ = 0.0;
        }
        return value_;
    }
    const double rate = (value - value_) / dtSeconds;
    derivative_ += smoothingFactor(DERIVATIVE_CUTOFF_HZ, dtSeconds) * (rate - derivative_);
    const double cutoff = minCutoffHz_ + beta_ * std::fabs(derivative_);
    value_ += smoothingFactor(cutoff, dtSeconds) * (value - value_);
    return value_;
}

void OneEuroFilter::reset() {
    primed_ = false;
}

StrokeSmoother::StrokeSmoother(int level, double width, double height)
    : enabled_(level > 0),
      width_(std::max(width, 1.0)),
      height_(std::max(height, 1.0)),
      x_(positionCutoff(level), positionBeta(level)),
      y_(positionCutoff(level), positionBeta(level)),
      pressure_(PRESSURE_CUTOFF_HZ, 0.0) {}

void StrokeSmoother::reset() {
    active_ = false;
    x_.reset();
    y_.reset();
    pressure_.reset();
}

PenSample StrokeSmoother::apply(const PenSample& sample) {
    if (!enabled_) {
        return sample;
    }
    if (!sample.inContact()) {
        reset();
        return sample;
    }
    const int64_t stepUs = sample.timeUs - lastTimeUs_;
    if (sample.type == PACKET_TYPE_DOWN || !active_ || stepUs > MAX_STEP_US || stepUs < -MAX_STEP_US) {
        reset();
    }

    const double dt  = active_ ? static_cast<double>(stepUs) / 1e6 : 0.0;
    PenSample    out = sample;
    out.x            = x_.filter(sample.x * width_, dt) / width_;
    out.y            = y_.filter(sample.y * height_, dt) / height_;
    out.pressure     = pressure_.filter(sample.pressure, dt);

    active_     = true;
    lastTimeUs_ = sample.timeUs;
    return out;
}

} // namespace stylus_cast
