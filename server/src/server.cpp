#include "stylus_cast/server.hpp"

#include "stylus_cast/realtime.hpp"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace stylus_cast {

namespace {

// The client re-sends its position every 100ms while the pen touches, so this much silence
// mid-stroke means the link is gone and the button must be released.
constexpr int64_t STROKE_TIMEOUT_MS    = 500;
constexpr int64_t PROXIMITY_TIMEOUT_MS = 1500;
constexpr int64_t STATS_INTERVAL_US    = 2'000'000;
constexpr int64_t IDLE_WAIT_US         = 50'000;
constexpr int     RECEIVE_BUFFER_BYTES = 256 * 1024;
// Bounds work per wake so a flood cannot starve playout, stats or shutdown.
constexpr int MAX_DATAGRAMS_PER_WAKE   = 256;
constexpr int MAX_TCP_PACKETS_PER_WAKE = 256;

#ifdef _WIN32
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
using SockLen = int;
#else
using SockLen = socklen_t;
#endif

void closeSock(
#ifdef _WIN32
    SOCKET& s
#else
    int& s
#endif
) {
#ifdef _WIN32
    if (s != INVALID_SOCKET) {
        ::closesocket(s);
        s = INVALID_SOCKET;
    }
#else
    if (s >= 0) {
        ::close(s);
        s = -1;
    }
#endif
}

bool setNonBlocking(
#ifdef _WIN32
    SOCKET s
#else
    int s
#endif
) {
#ifdef _WIN32
    u_long mode = 1;
    return ::ioctlsocket(s, FIONBIO, &mode) != SOCKET_ERROR;
#else
    const int flags = ::fcntl(s, F_GETFL, 0);
    return flags >= 0 && ::fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool wouldBlock() {
#ifdef _WIN32
    return ::WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

void printSocketError(const char* what) {
#ifdef _WIN32
    std::cerr << what << " (WSA " << ::WSAGetLastError() << ")\n";
#else
    std::perror(what);
#endif
}

std::string formatIpv4(const in_addr& addr) {
    char text[INET_ADDRSTRLEN] = {};
#ifdef _WIN32
    ::InetNtopA(AF_INET, &addr, text, sizeof(text));
#else
    ::inet_ntop(AF_INET, &addr, text, sizeof(text));
#endif
    return text;
}

} // namespace

Server::Server(const ServerConfig& config, ScreenRect activeRect, ScreenRect monitorBounds, uintptr_t targetMonitor)
    : config_(config),
      buffer_(config.delayUs),
      smoother_(config.smoothing, static_cast<double>(activeRect.width), static_cast<double>(activeRect.height)),
      injector_(activeRect, monitorBounds, targetMonitor, config.dryRun) {}

Server::~Server() {
    closeTcpClient();
#ifdef _WIN32
    if (timer_ != nullptr) {
        ::CloseHandle(timer_);
    }
    if (tcpClientEvent_ != WSA_INVALID_EVENT) {
        ::WSACloseEvent(tcpClientEvent_);
    }
    if (tcpListenEvent_ != WSA_INVALID_EVENT) {
        ::WSACloseEvent(tcpListenEvent_);
    }
    if (udpEvent_ != WSA_INVALID_EVENT) {
        ::WSACloseEvent(udpEvent_);
    }
#else
    if (queue_ >= 0) {
        ::close(queue_);
    }
#endif
    closeSock(tcpListenSocket_);
    closeSock(udpSocket_);
}

const std::atomic<bool>& Server::usbClientConnected() const {
    return usbConnected_;
}

uint64_t Server::totalPackets() const {
    return total_;
}

uint64_t Server::malformedPackets() const {
    return malformed_;
}

bool Server::open() {
    if (!config_.tracePath.empty() && !trace_.open(config_.tracePath)) {
        std::cerr << "Failed to open trace file: " << config_.tracePath << "\n";
        return false;
    }

#ifndef _WIN32
    queue_ = ::kqueue();
    if (queue_ < 0) {
        std::perror("Failed to create kqueue");
        return false;
    }
#else
    timer_ = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer_ == nullptr) {
        timer_ = ::CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    if (timer_ == nullptr) {
        std::cerr << "Failed to create waitable timer (error " << ::GetLastError() << ")\n";
        return false;
    }
#endif

    if (config_.udpEnabled) {
        udpSocket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udpSocket_ == INVALID_SOCK) {
            printSocketError("Failed to create UDP socket");
            return false;
        }
#ifdef _WIN32
        const BOOL reuse = TRUE;
        ::setsockopt(udpSocket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        ::setsockopt(udpSocket_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&RECEIVE_BUFFER_BYTES),
                     sizeof(RECEIVE_BUFFER_BYTES));
#else
        const int reuse = 1;
        ::setsockopt(udpSocket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        ::setsockopt(udpSocket_, SOL_SOCKET, SO_RCVBUF, &RECEIVE_BUFFER_BYTES, sizeof(RECEIVE_BUFFER_BYTES));
#endif
        if (!setNonBlocking(udpSocket_)) {
            printSocketError("Failed to set UDP socket non-blocking");
            return false;
        }

        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port        = htons(config_.udpPort);
        if (::bind(udpSocket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            printSocketError("Failed to bind UDP port");
            return false;
        }

#ifdef _WIN32
        udpEvent_ = ::WSACreateEvent();
        if (udpEvent_ == WSA_INVALID_EVENT || ::WSAEventSelect(udpSocket_, udpEvent_, FD_READ) == SOCKET_ERROR) {
            printSocketError("Failed to attach UDP socket event");
            return false;
        }
#else
        struct kevent readable {};
        EV_SET(&readable, static_cast<uintptr_t>(udpSocket_), EVFILT_READ, EV_ADD, 0, 0, nullptr);
        if (::kevent(queue_, &readable, 1, nullptr, 0, nullptr) < 0) {
            std::perror("Failed to watch UDP socket");
            return false;
        }
#endif
    }

    if (config_.usbEnabled) {
        tcpListenSocket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (tcpListenSocket_ == INVALID_SOCK) {
            printSocketError("Failed to create USB/TCP listen socket");
            return false;
        }
#ifdef _WIN32
        const BOOL reuse = TRUE;
        ::setsockopt(tcpListenSocket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
        const int reuse = 1;
        ::setsockopt(tcpListenSocket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
        if (!setNonBlocking(tcpListenSocket_)) {
            printSocketError("Failed to set TCP listen socket non-blocking");
            return false;
        }

        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port        = htons(config_.udpPort);
        if (::bind(tcpListenSocket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            printSocketError("Failed to bind USB/TCP port");
            return false;
        }
        if (::listen(tcpListenSocket_, 4) != 0) {
            printSocketError("Failed to listen on USB/TCP port");
            return false;
        }

#ifdef _WIN32
        tcpListenEvent_ = ::WSACreateEvent();
        tcpClientEvent_ = ::WSACreateEvent();
        if (tcpListenEvent_ == WSA_INVALID_EVENT || tcpClientEvent_ == WSA_INVALID_EVENT ||
            ::WSAEventSelect(tcpListenSocket_, tcpListenEvent_, FD_ACCEPT) == SOCKET_ERROR) {
            printSocketError("Failed to attach TCP listen socket event");
            return false;
        }
#else
        struct kevent readable {};
        EV_SET(&readable, static_cast<uintptr_t>(tcpListenSocket_), EVFILT_READ, EV_ADD, 0, 0, nullptr);
        if (::kevent(queue_, &readable, 1, nullptr, 0, nullptr) < 0) {
            std::perror("Failed to watch TCP listen socket");
            return false;
        }
#endif
    }

    return true;
}

void Server::run(const std::atomic<bool>& running) {
    if (!makeThreadRealtime()) {
        std::cerr << "[Warning] Real-time thread priority unavailable; timing may be less even.\n";
    }
    lastStatsUs_ = monotonicMicros();
    while (running.load(std::memory_order_relaxed)) {
        waitForWork();
        receivePending();
        playDue();
        checkSilence();
        if (config_.showStats) {
            printStats();
        }
    }
    injector_.streamLost();
    endThreadRealtime();
}

void Server::waitForWork() {
    if (morePending_) {
        morePending_ = false;
        return;
    }

    int64_t waitUs = IDLE_WAIT_US;
    if (const auto due = buffer_.nextDueUs()) {
        waitUs = std::clamp<int64_t>(*due - monotonicMicros(), 0, IDLE_WAIT_US);
    }
    if (waitUs == 0) {
        return;
    }

#ifdef _WIN32
    LARGE_INTEGER dueTime{};
    dueTime.QuadPart = -(waitUs * 10LL);
    ::SetWaitableTimer(timer_, &dueTime, 0, nullptr, nullptr, FALSE);

    HANDLE handles[4]{};
    DWORD  count = 0;
    if (udpEvent_ != WSA_INVALID_EVENT) {
        handles[count++] = udpEvent_;
    }
    if (tcpListenEvent_ != WSA_INVALID_EVENT) {
        handles[count++] = tcpListenEvent_;
    }
    if (tcpClientSocket_ != INVALID_SOCK && tcpClientEvent_ != WSA_INVALID_EVENT) {
        handles[count++] = tcpClientEvent_;
    }
    handles[count++] = timer_;
    ::WaitForMultipleObjects(count, handles, FALSE, INFINITE);
#else
    struct kevent timer {};
    // NOTE_CRITICAL opts out of timer coalescing.
    EV_SET(&timer, 1, EVFILT_TIMER, EV_ADD | EV_ONESHOT, NOTE_USECONDS | NOTE_CRITICAL, waitUs, nullptr);
    struct kevent event {};
    ::kevent(queue_, &timer, 1, &event, 1, nullptr);
#endif
}

void Server::receivePending() {
    if (tcpListenSocket_ != INVALID_SOCK) {
#ifdef _WIN32
        WSANETWORKEVENTS listenEvents{};
        ::WSAEnumNetworkEvents(tcpListenSocket_, tcpListenEvent_, &listenEvents);
#endif
        acceptTcp();
    }

    if (tcpClientSocket_ != INVALID_SOCK) {
#ifdef _WIN32
        WSANETWORKEVENTS clientEvents{};
        ::WSAEnumNetworkEvents(tcpClientSocket_, tcpClientEvent_, &clientEvents);
#endif
        receiveTcp();
    }

    if (udpSocket_ != INVALID_SOCK) {
#ifdef _WIN32
        WSANETWORKEVENTS udpEvents{};
        ::WSAEnumNetworkEvents(udpSocket_, udpEvent_, &udpEvents);
#endif
        receiveUdp();
    }
}

void Server::acceptTcp() {
    while (true) {
        sockaddr_in  peer{};
        SockLen      len  = sizeof(peer);
        SocketHandle conn = ::accept(tcpListenSocket_, reinterpret_cast<sockaddr*>(&peer), &len);
        if (conn == INVALID_SOCK) {
            return;
        }

        if (tcpClientSocket_ != INVALID_SOCK) {
            closeTcpClient();
        }

        if (!setNonBlocking(conn)) {
            closeSock(conn);
            continue;
        }

#ifdef _WIN32
        const BOOL nodelay = TRUE;
        ::setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
        ::setsockopt(conn, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&RECEIVE_BUFFER_BYTES),
                     sizeof(RECEIVE_BUFFER_BYTES));
        if (::WSAEventSelect(conn, tcpClientEvent_, FD_READ | FD_CLOSE) == SOCKET_ERROR) {
            closeSock(conn);
            continue;
        }
#else
        const int one = 1;
        ::setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        ::setsockopt(conn, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
        ::setsockopt(conn, SOL_SOCKET, SO_RCVBUF, &RECEIVE_BUFFER_BYTES, sizeof(RECEIVE_BUFFER_BYTES));
        struct kevent readable {};
        EV_SET(&readable, static_cast<uintptr_t>(conn), EVFILT_READ, EV_ADD, 0, 0, nullptr);
        if (::kevent(queue_, &readable, 1, nullptr, 0, nullptr) < 0) {
            closeSock(conn);
            continue;
        }
#endif

        tcpClientSocket_ = conn;
        tcpPeerAddr_     = peer.sin_addr;
        tcpBufLen_       = 0;
        usbConnected_.store(true, std::memory_order_relaxed);
        if (config_.verbose) {
            std::cout << "[USB] Client connected over TCP (" << formatIpv4(tcpPeerAddr_) << ")" << std::endl;
        }
    }
}

void Server::closeTcpClient() {
    if (tcpClientSocket_ == INVALID_SOCK) {
        return;
    }
#ifdef _WIN32
    if (tcpClientEvent_ != WSA_INVALID_EVENT) {
        ::WSAEventSelect(tcpClientSocket_, nullptr, 0);
        ::WSAResetEvent(tcpClientEvent_);
    }
#else
    struct kevent removeEv {};
    EV_SET(&removeEv, static_cast<uintptr_t>(tcpClientSocket_), EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    ::kevent(queue_, &removeEv, 1, nullptr, 0, nullptr);
#endif
    closeSock(tcpClientSocket_);
    tcpBufLen_ = 0;
    usbConnected_.store(false, std::memory_order_relaxed);

    if (activeIsUsb_) {
        injector_.streamLost();
        smoother_.reset();
        buffer_.reset();
        filter_.reset();
        activeIsUsb_ = false;
    }
    if (config_.verbose) {
        std::cout << "[USB] Client disconnected" << std::endl;
    }
}

void Server::receiveTcp() {
    uint8_t chunk[sizeof(StylusPacket) * 64];
    int     packetsHandled = 0;

    while (packetsHandled < MAX_TCP_PACKETS_PER_WAKE) {
        const size_t maxRead = std::min(sizeof(chunk), static_cast<size_t>(MAX_TCP_PACKETS_PER_WAKE - packetsHandled) *
                                                           sizeof(StylusPacket));
#ifdef _WIN32
        const int n = ::recv(tcpClientSocket_, reinterpret_cast<char*>(chunk), static_cast<int>(maxRead), 0);
#else
        const ssize_t n = ::recv(tcpClientSocket_, chunk, maxRead, 0);
#endif
        if (n == 0) {
            closeTcpClient();
            return;
        }
        if (n < 0) {
            if (!wouldBlock()) {
                closeTcpClient();
            }
            return;
        }

        const int64_t nowUs  = monotonicMicros();
        size_t        offset = 0;
        const size_t  total  = static_cast<size_t>(n);

        if (tcpBufLen_ > 0) {
            const size_t need = std::min(sizeof(StylusPacket) - tcpBufLen_, total);
            std::memcpy(tcpBuf_.data() + tcpBufLen_, chunk, need);
            tcpBufLen_ += need;
            offset += need;
            if (tcpBufLen_ == sizeof(StylusPacket)) {
                handlePacket(tcpBuf_.data(), sizeof(StylusPacket), tcpPeerAddr_, true, nowUs);
                tcpBufLen_ = 0;
                ++packetsHandled;
            }
        }

        while (offset + sizeof(StylusPacket) <= total) {
            handlePacket(chunk + offset, sizeof(StylusPacket), tcpPeerAddr_, true, nowUs);
            offset += sizeof(StylusPacket);
            ++packetsHandled;
        }

        if (offset < total) {
            tcpBufLen_ = total - offset;
            std::memcpy(tcpBuf_.data(), chunk + offset, tcpBufLen_);
        }

        if (static_cast<size_t>(n) < maxRead) {
            return;
        }
    }
    morePending_ = true;
}

void Server::receiveUdp() {
    uint8_t data[64];
    for (int i = 0; i < MAX_DATAGRAMS_PER_WAKE; ++i) {
        sockaddr_in from{};
        SockLen     length = sizeof(from);
#ifdef _WIN32
        const int size = ::recvfrom(udpSocket_, reinterpret_cast<char*>(data), sizeof(data), 0,
                                    reinterpret_cast<sockaddr*>(&from), &length);
#else
        const ssize_t size =
            ::recvfrom(udpSocket_, data, sizeof(data), 0, reinterpret_cast<sockaddr*>(&from), &length);
#endif
        if (size < 0) {
            if (!wouldBlock()) {
                // Transient socket error; drain stops for this wake.
            }
            return;
        }
        handlePacket(data, static_cast<size_t>(size), from.sin_addr, false, monotonicMicros());
    }
    morePending_ = true;
}

void Server::handlePacket(const uint8_t* data, size_t size, const in_addr& from, bool isUsb, int64_t nowUs) {
    StylusPacket packet{};
    if (!decodePacket(data, size, packet)) {
        ++malformed_;
        return;
    }
    if (!isUsb && config_.allowedClient != 0 && from.s_addr != config_.allowedClient) {
        ++rejected_;
        return;
    }
    if (isUsb != activeIsUsb_ && !injector_.isDown()) {
        filter_.reset();
    }

    // Distinguish USB TCP streams from UDP streams even if both originate on localhost/same IP.
    const uint32_t streamKey = isUsb ? (from.s_addr ^ 0x01000000u) : from.s_addr;
    switch (filter_.check(streamKey, packet.seq, nowUs / 1000)) {
        case Verdict::Foreign:
            ++rejected_;
            return;
        case Verdict::Stale:
            ++stale_;
            return;
        case Verdict::NewSession:
            buffer_.reset();
            smoother_.reset();
            injector_.streamLost();
            activeIsUsb_ = isUsb;
            if (config_.verbose) {
                std::cout << "[Client] Active " << (isUsb ? "USB" : "UDP") << " stream from " << formatIpv4(from)
                          << " (initial seq=" << packet.seq << ")" << std::endl;
            }
            break;
        case Verdict::Accept:
            break;
    }

    lastPacket_ = packet;
    ++interval_;
    ++total_;
    trace_.input(nowUs, packet);
    if (config_.verbose) {
        logTransition(packet);
    }
    buffer_.push(toSample(packet), packet.timestamp, nowUs);
}

void Server::playDue() {
    while (const auto sample = buffer_.popDue(monotonicMicros())) {
        const PenSample smoothed = smoother_.apply(*sample);
        trace_.output(monotonicMicros(), smoothed);
        injector_.inject(smoothed);
    }
}

void Server::checkSilence() {
    const int64_t silentMs   = filter_.silentMs(monotonicMicros() / 1000);
    const bool    strokeLost = injector_.isDown() && silentMs > STROKE_TIMEOUT_MS;
    const bool    penGone    = injector_.inProximity() && silentMs > PROXIMITY_TIMEOUT_MS;
    if (!strokeLost && !penGone) {
        return;
    }
    if (strokeLost && config_.verbose) {
        std::cout << "[Stylus] Released: no packets for " << silentMs << "ms mid-stroke" << std::endl;
    }
    injector_.streamLost();
    smoother_.reset();
}

void Server::logTransition(const StylusPacket& packet) const {
    if (packet.type != PACKET_TYPE_DOWN && packet.type != PACKET_TYPE_UP) {
        return;
    }
    std::cout << "[Stylus] " << (packet.type == PACKET_TYPE_DOWN ? "DOWN" : "UP  ") << " seq=" << packet.seq
              << " x=" << packet.x << " y=" << packet.y << " pressure=" << packet.pressure << " tilt=("
              << static_cast<int>(packet.tiltX) << "," << static_cast<int>(packet.tiltY) << ") buttons=0x" << std::hex
              << static_cast<int>(packet.buttons) << std::dec << "\n";
}

void Server::printStats() {
    const int64_t nowUs   = monotonicMicros();
    const int64_t elapsed = nowUs - lastStatsUs_;
    if (elapsed < STATS_INTERVAL_US) {
        return;
    }
    const JitterStats jitter = buffer_.jitter();
    std::cout << std::fixed << std::setprecision(1)
              << "[Stats] Rate: " << static_cast<double>(interval_) * 1e6 / static_cast<double>(elapsed) << " pkt/s"
              << " | Total: " << total_ << " | Lost: " << filter_.lost() << " | Stale: " << stale_
              << " | Malformed: " << malformed_ << " | Rejected: " << rejected_ << " | Jitter ms p50/p95/p99/max: "
              << jitter.p50Ms << "/" << jitter.p95Ms << "/" << jitter.p99Ms << "/" << jitter.maxMs
              << " | Delay: " << static_cast<double>(buffer_.delayUs()) / 1000.0 << "ms | Late: "
              << buffer_.lateCount() << " | Filled: " << buffer_.interpolatedCount();
    if (total_ > 0) {
        std::cout << " | Link: " << (activeIsUsb_ ? "USB" : "UDP")
                  << " | Last: type=" << static_cast<int>(lastPacket_.type) << " pos=(" << lastPacket_.x << ","
                  << lastPacket_.y << ") p=" << std::setprecision(2) << normalizeU16(lastPacket_.pressure);
    }
    std::cout << std::endl;
    interval_    = 0;
    lastStatsUs_ = nowUs;
}

} // namespace stylus_cast
