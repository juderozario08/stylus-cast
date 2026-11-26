#include "check.hpp"
#include "stylus_cast/stream_filter.hpp"

namespace stylus_cast::test {

void streamFilterTests() {
    StreamFilter f;
    CHECK(f.check(1, 1, 0) == Verdict::NewSession);
    CHECK(f.check(1, 500, 10) == Verdict::Accept);
    CHECK(f.check(1, 1000, 20) == Verdict::Accept);
    CHECK(f.lost() == 997);
    CHECK(f.check(1, 1000, 30) == Verdict::Stale);
    CHECK(f.check(2, 5, 40) == Verdict::Foreign); // cannot hijack a live stream
    CHECK(f.check(1, 1001, 50) == Verdict::Accept);
    CHECK(f.check(1, 2000, 5000) == Verdict::Accept); // a gap across a pause is not loss
    CHECK(f.lost() == 997);
    CHECK(f.silentMs(5100) == 100);
    CHECK(f.check(1, 1, 9000) == Verdict::NewSession);  // client restarted
    CHECK(f.check(2, 7, 12000) == Verdict::NewSession); // idle: a new client may take over
    CHECK(f.check(1, 2, 12010) == Verdict::Foreign);

    // Transport switch reset allows immediate handover without waiting SESSION_IDLE_MS.
    f.reset();
    CHECK(f.check(3, 1, 12015) == Verdict::NewSession);
}

} // namespace stylus_cast::test
