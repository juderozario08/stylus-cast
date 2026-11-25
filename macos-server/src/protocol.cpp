#include "stylus_cast/protocol.hpp"

#include <algorithm>
#include <cstring>

namespace stylus_cast {

bool isKnownPacketType(uint8_t type) {
    return type <= PACKET_TYPE_UP;
}

bool decodePacket(const uint8_t* data, size_t size, StylusPacket& out) {
    if (size != sizeof(StylusPacket) || !isKnownPacketType(data[0])) {
        return false;
    }
    std::memcpy(&out, data, sizeof(StylusPacket));
    return true;
}

bool isSequenceNewer(uint32_t incoming, uint32_t last) {
    return incoming != last && (incoming - last) < 0x80000000u;
}

uint32_t sequenceGap(uint32_t incoming, uint32_t last) {
    return isSequenceNewer(incoming, last) ? incoming - last - 1u : 0u;
}

double normalizeU16(uint16_t value) {
    return static_cast<double>(value) / 65535.0;
}

double normalizeTilt(int8_t degrees) {
    return std::clamp(static_cast<double>(degrees), -90.0, 90.0) / 90.0;
}

} // namespace stylus_cast
