#include "stylus_cast/server.hpp"

#include "stylus_cast/realtime.hpp"

#include <arpa/inet.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
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
constexpr int MAX_DATAGRAMS_PER_WAKE = 256;

} // namespace

Server::Server(const ServerConfig& config, CGRect activeRect)
    : config_(config),
      buffer_(config.delayUs),
      smoother_(config.smoothing, activeRect.size.width, activeRect.size.height),
      injector_(activeRect, config.dryRun) {}

Server::~Server() {
    if (queue_ >= 0) {
        ::close(queue_);
    }
    if (socket_ >= 0) {
        ::close(socket_);
    }
}

bool Server::open() {
    if (!config_.tracePath.empty() && !trace_.open(config_.tracePath)) {
        std::perror("Failed to open trace file");
        return false;
    }
    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    queue_  = ::kqueue();
    if (socket_ < 0 || queue_ < 0) {
        std::perror("Failed to create UDP receiver");
        return false;
    }
    const int reuse = 1;
    ::setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ::setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &RECEIVE_BUFFER_BYTES, sizeof(RECEIVE_BUFFER_BYTES));

    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port        = htons(config_.udpPort);
    if (::bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        std::perror("Failed to bind UDP port");
        return false;
    }

    struct kevent readable {};
    EV_SET(&readable, static_cast<uintptr_t>(socket_), EVFILT_READ, EV_ADD, 0, 0, nullptr);
    if (::kevent(queue_, &readable, 1, nullptr, 0, nullptr) < 0) {
        std::perror("Failed to watch UDP socket");
        return false;
    }
    return true;
}

void Server::run(const std::atomic<bool>& running) {
    if (!makeThreadRealtime()) {
        std::cerr << "[Warning] Real-time scheduling unavailable; timing may be less even.\n";
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
}

void Server::waitForWork() {
    int64_t waitUs = IDLE_WAIT_US;
    if (const auto due = buffer_.nextDueUs()) {
        waitUs = std::clamp<int64_t>(*due - monotonicMicros(), 0, IDLE_WAIT_US);
    }
    struct kevent timer {};
    // NOTE_CRITICAL opts out of timer coalescing.
    EV_SET(&timer, 1, EVFILT_TIMER, EV_ADD | EV_ONESHOT, NOTE_USECONDS | NOTE_CRITICAL, waitUs, nullptr);
    struct kevent event {};
    ::kevent(queue_, &timer, 1, &event, 1, nullptr);
}

void Server::receivePending() {
    uint8_t data[64];
    for (int i = 0; i < MAX_DATAGRAMS_PER_WAKE; ++i) {
        sockaddr_in   from{};
        socklen_t     length = sizeof(from);
        const ssize_t size =
            ::recvfrom(socket_, data, sizeof(data), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&from), &length);
        if (size < 0) {
            return;
        }
        handleDatagram(data, static_cast<size_t>(size), from.sin_addr, monotonicMicros());
    }
}

void Server::handleDatagram(const uint8_t* data, size_t size, const in_addr& from, int64_t nowUs) {
    StylusPacket packet{};
    if (!decodePacket(data, size, packet)) {
        ++malformed_;
        return;
    }
    if (config_.allowedClient != 0 && from.s_addr != config_.allowedClient) {
        ++rejected_;
        return;
    }
    switch (filter_.check(from.s_addr, packet.seq, nowUs / 1000)) {
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
            if (config_.verbose) {
                char text[INET_ADDRSTRLEN] = {};
                ::inet_ntop(AF_INET, &from, text, sizeof(text));
                std::cout << "[Client] Active stream from " << text << " (initial seq=" << packet.seq << ")"
                          << std::endl;
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
    const int64_t silentMs = filter_.silentMs(monotonicMicros() / 1000);
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
        std::cout << " | Last: type=" << static_cast<int>(lastPacket_.type) << " pos=(" << lastPacket_.x << ","
                  << lastPacket_.y << ") p=" << std::setprecision(2) << normalizeU16(lastPacket_.pressure);
    }
    std::cout << std::endl;
    interval_    = 0;
    lastStatsUs_ = nowUs;
}

} // namespace stylus_cast
