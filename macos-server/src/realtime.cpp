#include "stylus_cast/realtime.hpp"

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>

namespace stylus_cast {

namespace {

const mach_timebase_info_data_t& timebase() {
    static const mach_timebase_info_data_t info = [] {
        mach_timebase_info_data_t value{};
        mach_timebase_info(&value);
        return value;
    }();
    return info;
}

uint32_t nanosToTicks(uint64_t nanos) {
    return static_cast<uint32_t>(nanos * timebase().denom / timebase().numer);
}

} // namespace

bool makeThreadRealtime() {
    thread_time_constraint_policy_data_t policy{};
    policy.period      = nanosToTicks(4'000'000);
    policy.computation = nanosToTicks(500'000);
    policy.constraint  = nanosToTicks(2'000'000);
    policy.preemptible = 1;
    return thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                             reinterpret_cast<thread_policy_t>(&policy),
                             THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;
}

int64_t monotonicMicros() {
    return static_cast<int64_t>(mach_absolute_time() * timebase().numer / timebase().denom / 1000);
}

} // namespace stylus_cast
