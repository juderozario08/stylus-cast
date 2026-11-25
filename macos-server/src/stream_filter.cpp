#include "stylus_cast/stream_filter.hpp"

#include "stylus_cast/protocol.hpp"

namespace stylus_cast {

Verdict StreamFilter::check(uint32_t clientAddr, uint32_t seq, int64_t nowMs) {
    const bool idle       = !active_ || (nowMs - lastMs_) > SESSION_IDLE_MS;
    const bool sameClient = active_ && clientAddr == addr_;

    if (!sameClient && !idle) {
        return Verdict::Foreign;
    }

    const bool newSession = !sameClient || (idle && seq < lastSeq_);
    if (!newSession) {
        if (!isSequenceNewer(seq, lastSeq_)) {
            return Verdict::Stale;
        }
        if (!idle) { // a gap spanning a pause is not packet loss
            lost_ += sequenceGap(seq, lastSeq_);
        }
    }

    active_  = true;
    addr_    = clientAddr;
    lastSeq_ = seq;
    lastMs_  = nowMs;
    return newSession ? Verdict::NewSession : Verdict::Accept;
}

uint64_t StreamFilter::lost() const {
    return lost_;
}

int64_t StreamFilter::silentMs(int64_t nowMs) const {
    return active_ ? nowMs - lastMs_ : 0;
}

} // namespace stylus_cast
