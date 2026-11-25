#include "penbridge/beacon.hpp"

#include "penbridge/protocol.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace penbridge {

namespace {

// Directed broadcast address of every active interface, plus the limited broadcast, so
// discovery works regardless of the default route.
std::vector<in_addr> broadcastTargets() {
    std::vector<in_addr> targets;
    ifaddrs*             interfaces = nullptr;
    if (::getifaddrs(&interfaces) == 0) {
        for (const ifaddrs* it = interfaces; it != nullptr; it = it->ifa_next) {
            constexpr unsigned REQUIRED = IFF_UP | IFF_BROADCAST;
            if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET || it->ifa_broadaddr == nullptr ||
                (it->ifa_flags & REQUIRED) != REQUIRED || (it->ifa_flags & IFF_LOOPBACK) != 0) {
                continue;
            }
            sockaddr_in address{};
            std::memcpy(&address, it->ifa_broadaddr, sizeof(address));
            targets.push_back(address.sin_addr);
        }
        ::freeifaddrs(interfaces);
    }
    in_addr limited{};
    limited.s_addr = htonl(INADDR_BROADCAST);
    targets.push_back(limited);
    return targets;
}

// Sends one beacon to each target and returns a description of the outcome.
std::string broadcastOnce(int sock, uint16_t port) {
    std::string summary;
    for (const in_addr& target : broadcastTargets()) {
        sockaddr_in destination{};
        destination.sin_family = AF_INET;
        destination.sin_port   = htons(port);
        destination.sin_addr   = target;
        const ssize_t sent     = ::sendto(sock, BEACON_PAYLOAD, BEACON_PAYLOAD_LEN, 0,
                                          reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
        const int error = errno;

        char text[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &target, text, sizeof(text));
        summary += std::string(" ") + text;
        if (sent < 0) {
            summary += std::string(" (failed: ") + std::strerror(error) + ")";
        }
    }
    return summary;
}

} // namespace

Beacon::Beacon(uint16_t port, bool verbose) : port_(port), verbose_(verbose), thread_([this] { run(); }) {}

Beacon::~Beacon() {
    running_ = false;
    thread_.join();
}

void Beacon::run() {
    const int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        std::perror("[Beacon] Failed to create UDP broadcast socket");
        return;
    }
    const int enable = 1;
    if (::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable)) < 0) {
        std::perror("[Beacon] setsockopt(SO_BROADCAST) failed");
    }

    std::string lastSummary;
    while (running_) {
        const std::string summary = broadcastOnce(sock, port_);
        // Log only when the targets or their reachability change, not every second.
        if (verbose_ && summary != lastSummary) {
            std::cout << ("[Beacon] Broadcasting to:" + summary + "\n") << std::flush;
            lastSummary = summary;
        }
        for (int i = 0; i < 10 && running_; ++i) { // short sleeps so shutdown is prompt
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    ::close(sock);
}

} // namespace penbridge
