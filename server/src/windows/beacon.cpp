#include "stylus_cast/beacon.hpp"

#include "stylus_cast/protocol.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace stylus_cast {

namespace {

void addUniqueTarget(std::vector<in_addr>& targets, uint32_t sAddr) {
    const auto it = std::find_if(targets.begin(), targets.end(),
                                 [sAddr](const in_addr& addr) { return addr.s_addr == sAddr; });
    if (it == targets.end()) {
        in_addr addr{};
        addr.s_addr = sAddr;
        targets.push_back(addr);
    }
}

// Directed broadcast address of every active IPv4 interface, plus the limited broadcast,
// so discovery works regardless of the default route or Mobile Hotspot adapter.
std::vector<in_addr> broadcastTargets() {
    std::vector<in_addr> targets;

    ULONG                bufferBytes = 16 * 1024;
    std::vector<uint8_t> buffer(bufferBytes);
    ULONG                flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_DNS_SERVER;

    ULONG status = ::GetAdaptersAddresses(AF_INET, flags, nullptr,
                                          reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &bufferBytes);
    if (status == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(bufferBytes);
        status = ::GetAdaptersAddresses(AF_INET, flags, nullptr,
                                        reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &bufferBytes);
    }

    if (status == NO_ERROR) {
        for (const auto* adapter = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter != nullptr;
             adapter             = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }
            for (const auto* ua = adapter->FirstUnicastAddress; ua != nullptr; ua = ua->Next) {
                if (ua->Address.lpSockaddr == nullptr || ua->Address.lpSockaddr->sa_family != AF_INET ||
                    ua->OnLinkPrefixLength == 0 || ua->OnLinkPrefixLength >= 32) {
                    continue;
                }
                sockaddr_in sin{};
                std::memcpy(&sin, ua->Address.lpSockaddr, sizeof(sin));
                const uint32_t ipHost   = ntohl(sin.sin_addr.s_addr);
                const uint32_t maskHost = 0xFFFFFFFFu << (32u - ua->OnLinkPrefixLength);
                addUniqueTarget(targets, htonl(ipHost | ~maskHost));
            }
        }
    }

    addUniqueTarget(targets, htonl(INADDR_BROADCAST));
    return targets;
}

// Sends one beacon to each target and returns a description of the outcome.
std::string broadcastOnce(SOCKET sock, uint16_t port) {
    std::string summary;
    for (const in_addr& target : broadcastTargets()) {
        sockaddr_in destination{};
        destination.sin_family = AF_INET;
        destination.sin_port   = htons(port);
        destination.sin_addr   = target;
        const int sent =
            ::sendto(sock, BEACON_PAYLOAD, static_cast<int>(BEACON_PAYLOAD_LEN), 0,
                     reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
        const int error = sent == SOCKET_ERROR ? ::WSAGetLastError() : 0;

        char text[INET_ADDRSTRLEN] = {};
        ::InetNtopA(AF_INET, &target, text, sizeof(text));
        summary += std::string(" ") + text;
        if (sent == SOCKET_ERROR) {
            summary += " (failed: WSA " + std::to_string(error) + ")";
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
    const SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[Beacon] Failed to create UDP broadcast socket (WSA " << ::WSAGetLastError() << ")\n";
        return;
    }
    const BOOL enable = TRUE;
    if (::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enable), sizeof(enable)) ==
        SOCKET_ERROR) {
        std::cerr << "[Beacon] setsockopt(SO_BROADCAST) failed (WSA " << ::WSAGetLastError() << ")\n";
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
    ::closesocket(sock);
}

} // namespace stylus_cast
