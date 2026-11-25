#include "penbridge/trace.hpp"

namespace penbridge {

Trace::~Trace() {
    if (file_ != nullptr) {
        std::fclose(file_);
    }
}

bool Trace::open(const std::string& path) {
    file_ = std::fopen(path.c_str(), "w");
    if (file_ != nullptr) {
        std::fputs("dir,now_us,type,x,y,pressure,seq,timestamp\n", file_);
    }
    return file_ != nullptr;
}

void Trace::input(int64_t nowUs, const StylusPacket& packet) {
    if (file_ != nullptr) {
        const PenSample s = toSample(packet);
        std::fprintf(file_, "in,%lld,%u,%.6f,%.6f,%.4f,%u,%u\n", static_cast<long long>(nowUs), s.type, s.x, s.y,
                     s.pressure, packet.seq, packet.timestamp);
    }
}

void Trace::output(int64_t nowUs, const PenSample& sample) {
    if (file_ != nullptr) {
        std::fprintf(file_, "out,%lld,%u,%.6f,%.6f,%.4f,,\n", static_cast<long long>(nowUs), sample.type, sample.x,
                     sample.y, sample.pressure);
    }
}

} // namespace penbridge
