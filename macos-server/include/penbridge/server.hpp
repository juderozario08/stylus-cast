#pragma once

#include "penbridge/config.hpp"
#include "penbridge/injector.hpp"
#include "penbridge/jitter_buffer.hpp"
#include "penbridge/stream_filter.hpp"
#include "penbridge/stroke_smoother.hpp"
#include "penbridge/trace.hpp"

#include <ApplicationServices/ApplicationServices.h>
#include <netinet/in.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace penbridge {

// Receives packets, replays them at even timing and injects them, on one real-time thread.
class Server {
public:
    Server(const ServerConfig& config, CGRect activeRect);
    ~Server();
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    bool open(); // bind the socket; prints the reason on failure
    void run(const std::atomic<bool>& running);

private:
    void waitForWork();
    void receivePending();
    void handleDatagram(const uint8_t* data, size_t size, const in_addr& from, int64_t nowUs);
    void playDue();
    void checkSilence();
    void printStats();
    void logTransition(const StylusPacket& packet) const;

    ServerConfig   config_;
    StreamFilter   filter_;
    JitterBuffer   buffer_;
    StrokeSmoother smoother_;
    Injector       injector_;
    Trace          trace_;

    int          socket_       = -1;
    int          queue_        = -1;
    StylusPacket lastPacket_   = {};
    int64_t      lastStatsUs_  = 0;
    uint64_t     interval_     = 0;
    uint64_t     total_        = 0;
    uint64_t     malformed_    = 0;
    uint64_t     stale_        = 0;
    uint64_t     rejected_     = 0;
};

} // namespace penbridge
