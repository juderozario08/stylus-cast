#pragma once

#include "stylus_cast/pen_sample.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

namespace stylus_cast {

// Optional CSV recording of every received ("in") and replayed ("out") sample.
class Trace {
public:
    Trace() = default;
    ~Trace();
    Trace(const Trace&)            = delete;
    Trace& operator=(const Trace&) = delete;

    bool open(const std::string& path);
    void input(int64_t nowUs, const StylusPacket& packet);
    void output(int64_t nowUs, const PenSample& sample);

private:
    std::FILE* file_ = nullptr;
};

} // namespace stylus_cast
