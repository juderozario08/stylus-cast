#include "check.hpp"
#include "penbridge/protocol.hpp"

#include <cstring>

namespace penbridge::test {

void protocolTests() {
    StylusPacket packet{};
    packet.type      = PACKET_TYPE_MOVE;
    packet.buttons   = BUTTON_PRIMARY | BUTTON_ERASER;
    packet.x         = 0x1234;
    packet.y         = 0xABCD;
    packet.pressure  = 0xFFFF;
    packet.tiltX     = -45;
    packet.tiltY     = 30;
    packet.seq       = 0x01020304;
    packet.timestamp = 0x5678;

    const uint8_t wire[16] = {2, 3, 0x34, 0x12, 0xCD, 0xAB, 0xFF, 0xFF, 0xD3, 30, 4, 3, 2, 1, 0x78, 0x56};
    CHECK(std::memcmp(&packet, wire, sizeof(wire)) == 0);

    StylusPacket decoded{};
    CHECK(decodePacket(wire, 16, decoded) && decoded.seq == 0x01020304 && decoded.timestamp == 0x5678);
    CHECK(!decodePacket(wire, 15, decoded));
    uint8_t unknown[16] = {77};
    CHECK(!decodePacket(unknown, 16, decoded));
    CHECK(isKnownPacketType(3) && !isKnownPacketType(4) && !isKnownPacketType(255));

    CHECK(isSequenceNewer(2, 1) && !isSequenceNewer(1, 1) && !isSequenceNewer(1, 2));
    CHECK(isSequenceNewer(0, 0xFFFFFFFFu) && isSequenceNewer(5, 0xFFFFFFFEu));
    CHECK(!isSequenceNewer(0xFFFFFFFEu, 5));
    CHECK(sequenceGap(2, 1) == 0 && sequenceGap(500, 1) == 498);
    CHECK(sequenceGap(1, 0xFFFFFFFEu) == 2 && sequenceGap(1, 2) == 0);

    CHECK(normalizeU16(0) == 0.0 && normalizeU16(65535) == 1.0);
    CHECK(normalizeTilt(0) == 0.0 && normalizeTilt(90) == 1.0 && normalizeTilt(-90) == -1.0);
    CHECK(normalizeTilt(120) == 1.0 && normalizeTilt(-120) == -1.0);
}

} // namespace penbridge::test
