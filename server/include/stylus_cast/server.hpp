#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <netinet/in.h>
#endif

#include "stylus_cast/config.hpp"
#include "stylus_cast/display.hpp"
#include "stylus_cast/injector.hpp"
#include "stylus_cast/jitter_buffer.hpp"
#include "stylus_cast/stream_filter.hpp"
#include "stylus_cast/stroke_smoother.hpp"
#include "stylus_cast/trace.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace stylus_cast {

// Receives packets over UDP and/or USB (TCP via ADB reverse), replays them at even
// timing, and injects native OS stylus events on one real-time thread.
class Server {
public:
    Server(const ServerConfig& config, ScreenRect activeRect, ScreenRect monitorBounds, uintptr_t targetMonitor = 0);
    ~Server();
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    bool open(); // bind UDP/TCP sockets and create wait handles; prints reason on failure
    void run(const std::atomic<bool>& running);

    const std::atomic<bool>& usbClientConnected() const;
    uint64_t                 totalPackets() const;
    uint64_t                 malformedPackets() const;

private:
    void waitForWork();
    void receivePending();
    void receiveUdp();
    void acceptTcp();
    void receiveTcp();
    void closeTcpClient();
    void handlePacket(const uint8_t* data, size_t size, const in_addr& from, bool isUsb, int64_t nowUs);
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

#ifdef _WIN32
    using SocketHandle = SOCKET;
    static constexpr SocketHandle INVALID_SOCK = INVALID_SOCKET;
    WSAEVENT udpEvent_                         = WSA_INVALID_EVENT;
    WSAEVENT tcpListenEvent_                   = WSA_INVALID_EVENT;
    WSAEVENT tcpClientEvent_                   = WSA_INVALID_EVENT;
    HANDLE   timer_                            = nullptr;
#else
    using SocketHandle = int;
    static constexpr SocketHandle INVALID_SOCK = -1;
    int queue_                                 = -1;
#endif

    SocketHandle udpSocket_       = INVALID_SOCK;
    SocketHandle tcpListenSocket_ = INVALID_SOCK;
    SocketHandle tcpClientSocket_ = INVALID_SOCK;
    in_addr      tcpPeerAddr_{};
    bool         activeIsUsb_     = false;
    bool         morePending_     = false;

    std::array<uint8_t, sizeof(StylusPacket)> tcpBuf_{};
    size_t                                    tcpBufLen_ = 0;

    std::atomic<bool> usbConnected_{false};

    StylusPacket lastPacket_  = {};
    int64_t      lastStatsUs_ = 0;
    uint64_t     interval_    = 0;
    uint64_t     total_       = 0;
    uint64_t     malformed_   = 0;
    uint64_t     stale_       = 0;
    uint64_t     rejected_    = 0;
};

} // namespace stylus_cast
