#include "check.hpp"

#include <cmath>
#include <cstdio>

namespace stylus_cast::test {

namespace {
int g_failures = 0;
int g_checks   = 0;
} // namespace

void check(bool passed, const char* file, int line, const std::string& expression) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::fprintf(stderr, "[FAIL] %s:%d: %s\n", file, line, expression.c_str());
    }
}

int failureCount() {
    return g_failures;
}

bool near(double a, double b, double epsilon) {
    return std::fabs(a - b) <= epsilon;
}

} // namespace stylus_cast::test

int main() {
    using namespace stylus_cast::test;
    protocolTests();
    streamFilterTests();
    penStateTests();
    jitterBufferTests();
    strokeSmootherTests();
    tabletEventTests();
    if (failureCount() != 0) {
        std::fprintf(stderr, "[FAIL] %d check(s) failed\n", failureCount());
        return 1;
    }
    std::printf("[PASS] All Stylus-Cast tests passed (%d checks).\n", g_checks);
    return 0;
}
