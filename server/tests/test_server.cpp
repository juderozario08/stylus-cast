#include "check.hpp"
#include "stylus_cast/realtime.hpp"
#include "stylus_cast/server.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace stylus_cast::test {

namespace {

StylusPacket makePacket(uint8_t type, uint32_t seq, uint16_t x, uint16_t y) {
    StylusPacket p{};
    p.type      = type;
    p.x         = x;
    p.y         = y;
    p.pressure  = type == PACKET_TYPE_UP ? 0 : 32768;
    p.seq       = seq;
    p.timestamp = static_cast<uint16_t>(seq * 40);
    return p;
}

} // namespace

void serverTransportTests() {
    CHECK(platformInit());

    ServerConfig cfg;
    cfg.udpPort    = 19432;
    cfg.dryRun     = true;
    cfg.udpEnabled = true;
    cfg.usbEnabled = true;

    Server server(cfg, {0, 0, 1920, 1080}, {0, 0, 1920, 1080}, 0);
    CHECK(server.open());

    std::atomic<bool> running{true};
    std::thread       serverThread([&] { server.run(running); });

    // Connect a TCP client (simulating ADB reverse USB transport) and send split + coalesced frames.
#ifdef _WIN32
    SOCKET tcp = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(tcp != INVALID_SOCKET);
#else
    int tcp = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(tcp >= 0);
#endif

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(cfg.udpPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bool connected = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (::connect(tcp, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0) {
            connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(connected);

    if (connected) {
        uint8_t wire[sizeof(StylusPacket) * 3]{};
        const StylusPacket p1 = makePacket(PACKET_TYPE_DOWN, 1, 1000, 2000);
        const StylusPacket p2 = makePacket(PACKET_TYPE_MOVE, 2, 2000, 3000);
        const StylusPacket p3 = makePacket(PACKET_TYPE_UP, 3, 2000, 3000);
        std::memcpy(wire, &p1, sizeof(StylusPacket));
        std::memcpy(wire + sizeof(StylusPacket), &p2, sizeof(StylusPacket));
        std::memcpy(wire + sizeof(StylusPacket) * 2, &p3, sizeof(StylusPacket));

        // Send 10 bytes first (partial packet), then the remaining 38 bytes (rest of p1 + p2 + p3).
#ifdef _WIN32
        CHECK(::send(tcp, reinterpret_cast<const char*>(wire), 10, 0) == 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        CHECK(::send(tcp, reinterpret_cast<const char*>(wire + 10), 38, 0) == 38);
#else
        CHECK(::send(tcp, wire, 10, 0) == 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        CHECK(::send(tcp, wire + 10, 38, 0) == 38);
#endif

        for (int i = 0; i < 40 && server.totalPackets() < 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(server.usbClientConnected().load());
        CHECK(server.totalPackets() == 3);
        CHECK(server.malformedPackets() == 0);
    }

#ifdef _WIN32
    ::closesocket(tcp);
#else
    ::close(tcp);
#endif

    for (int i = 0; i < 40 && server.usbClientConnected().load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(!server.usbClientConnected().load());

    running.store(false);
    serverThread.join();
    platformCleanup();
}

} // namespace stylus_cast::test
