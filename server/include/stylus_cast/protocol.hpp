#pragma once

#include <cstddef>
#include <cstdint>

namespace stylus_cast {

// Packet types (offset 0)
inline constexpr uint8_t PACKET_TYPE_HOVER = 0; // ACTION_HOVER_MOVE
inline constexpr uint8_t PACKET_TYPE_DOWN  = 1; // ACTION_DOWN
inline constexpr uint8_t PACKET_TYPE_MOVE  = 2; // ACTION_MOVE / historical sample
inline constexpr uint8_t PACKET_TYPE_UP    = 3; // ACTION_UP / ACTION_CANCEL

// Button bits (offset 1)
inline constexpr uint8_t BUTTON_PRIMARY = 0x01; // S-Pen barrel button
inline constexpr uint8_t BUTTON_ERASER  = 0x02; // Eraser tool

inline constexpr uint16_t DEFAULT_PORT        = 9000;
inline constexpr uint16_t DEFAULT_UDP_PORT    = DEFAULT_PORT;
inline constexpr uint16_t DEFAULT_BEACON_PORT = 9001;
// Shared beacon payload so the Android client auto-discovers both macOS and Windows hosts.
inline constexpr char   BEACON_PAYLOAD[]   = "MAC_TABLET_SERVER";
inline constexpr size_t BEACON_PAYLOAD_LEN = sizeof(BEACON_PAYLOAD) - 1;

#pragma pack(push, 1)
struct StylusPacket {
    uint8_t  type;
    uint8_t  buttons;
    uint16_t x;
    uint16_t y;
    uint16_t pressure;
    int8_t   tiltX;     // degrees, positive = pen leans right
    int8_t   tiltY;     // degrees, positive = pen leans toward the user
    uint32_t seq;
    uint16_t timestamp; // sample time in 100us ticks (wraps every 6.5s); 0 = not provided
};
#pragma pack(pop)

static_assert(sizeof(StylusPacket) == 16);
static_assert(offsetof(StylusPacket, type) == 0);
static_assert(offsetof(StylusPacket, buttons) == 1);
static_assert(offsetof(StylusPacket, x) == 2);
static_assert(offsetof(StylusPacket, y) == 4);
static_assert(offsetof(StylusPacket, pressure) == 6);
static_assert(offsetof(StylusPacket, tiltX) == 8);
static_assert(offsetof(StylusPacket, tiltY) == 9);
static_assert(offsetof(StylusPacket, seq) == 10);
static_assert(offsetof(StylusPacket, timestamp) == 14);

// Decodes a datagram; false if the size or packet type is invalid.
bool decodePacket(const uint8_t* data, size_t size, StylusPacket& out);

bool isKnownPacketType(uint8_t type);

// RFC 1982 serial comparison: true if `incoming` is strictly newer than `last`.
bool isSequenceNewer(uint32_t incoming, uint32_t last);

// Number of packets skipped between `last` and a newer `incoming`; 0 if not newer.
uint32_t sequenceGap(uint32_t incoming, uint32_t last);

double normalizeU16(uint16_t value);  // [0, 65535] -> [0, 1]
double normalizeTilt(int8_t degrees); // [-90, 90]  -> [-1, 1]

} // namespace stylus_cast
