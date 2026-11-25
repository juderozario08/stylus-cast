#include "check.hpp"
#include "stylus_cast/stroke_smoother.hpp"

#include <cmath>
#include <random>
#include <vector>

namespace stylus_cast::test {

namespace {

constexpr double WIDTH  = 1440.0;
constexpr double HEIGHT = 900.0;

struct Point {
    double x;
    double y;
};

// Handwriting-like loops, ~40pt tall at ~4 strokes per second.
Point ink(double t) {
    return {200.0 + 120.0 * t + 12.0 * std::sin(2 * M_PI * 4.0 * t), 400.0 + 20.0 * std::cos(2 * M_PI * 4.0 * t)};
}

struct Quality {
    double roughness; // RMS second difference: visible jaggedness
    double error;     // RMS distance from the intended path, at the same instant
};

Quality measure(int level) {
    std::mt19937                     rng(7);
    std::normal_distribution<double> noise(0.0, 0.5);
    StrokeSmoother                   smoother(level, WIDTH, HEIGHT);
    std::vector<Point>               out;
    double                           error = 0.0;
    constexpr int                    COUNT = 600;
    for (int i = 0; i < COUNT; ++i) {
        const double t    = i / 250.0;
        const Point  want = ink(t);
        PenSample    s;
        s.type   = i == 0 ? PACKET_TYPE_DOWN : PACKET_TYPE_MOVE;
        s.timeUs = static_cast<int64_t>(i) * 4000;
        s.x      = (want.x + noise(rng)) / WIDTH;
        s.y      = (want.y + noise(rng)) / HEIGHT;
        s        = smoother.apply(s);
        out.push_back({s.x * WIDTH, s.y * HEIGHT});
        error += std::pow(out.back().x - want.x, 2) + std::pow(out.back().y - want.y, 2);
    }
    double rough = 0.0;
    for (size_t i = 2; i < out.size(); ++i) {
        rough += std::pow(out[i].x - 2 * out[i - 1].x + out[i - 2].x, 2) +
                 std::pow(out[i].y - 2 * out[i - 1].y + out[i - 2].y, 2);
    }
    return {std::sqrt(rough / static_cast<double>(out.size() - 2)), std::sqrt(error / COUNT)};
}

} // namespace

void strokeSmootherTests() {
    const Quality raw     = measure(0);
    const Quality smooth  = measure(StrokeSmoother::DEFAULT_LEVEL);
    CHECK(smooth.roughness < raw.roughness * 0.5);
    CHECK(smooth.error < 4.0);

    // Stroke endpoints stay exactly where the pen was.
    StrokeSmoother s(10, WIDTH, HEIGHT);
    PenSample      p;
    p.type = PACKET_TYPE_DOWN;
    p.x    = 0.25;
    CHECK(s.apply(p).x == 0.25);
    p.type   = PACKET_TYPE_MOVE;
    p.timeUs = 4000;
    p.x      = 0.5;
    CHECK(s.apply(p).x < 0.5);
    p.type = PACKET_TYPE_UP;
    CHECK(s.apply(p).x == 0.5);
    // ...and the next stroke is not dragged toward the previous one.
    p.type = PACKET_TYPE_DOWN;
    p.x    = 0.9;
    CHECK(s.apply(p).x == 0.9);
}

} // namespace stylus_cast::test
