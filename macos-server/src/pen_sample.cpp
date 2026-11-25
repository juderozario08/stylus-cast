#include "stylus_cast/pen_sample.hpp"

namespace stylus_cast {

bool PenSample::inContact() const {
    return type == PACKET_TYPE_DOWN || type == PACKET_TYPE_MOVE;
}

PenSample toSample(const StylusPacket& packet) {
    PenSample sample;
    sample.type     = packet.type;
    sample.buttons  = packet.buttons;
    sample.x        = normalizeU16(packet.x);
    sample.y        = normalizeU16(packet.y);
    sample.pressure = normalizeU16(packet.pressure);
    sample.tiltX    = normalizeTilt(packet.tiltX);
    sample.tiltY    = normalizeTilt(packet.tiltY);
    return sample;
}

} // namespace stylus_cast
