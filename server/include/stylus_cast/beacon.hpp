#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace stylus_cast {

// Broadcasts the discovery payload once per second on every active IPv4 interface.
class Beacon {
public:
    Beacon(uint16_t port, bool verbose);
    ~Beacon();
    Beacon(const Beacon&)            = delete;
    Beacon& operator=(const Beacon&) = delete;

private:
    void run();

    uint16_t          port_;
    bool              verbose_;
    std::atomic<bool> running_{true};
    std::thread       thread_;
};

} // namespace stylus_cast
