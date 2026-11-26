#include "stylus_cast/pen_state.hpp"

namespace stylus_cast {

void PenSteps::push(PenStep step) {
    if (count_ < items_.size()) {
        items_[count_++] = step;
    }
}

size_t PenSteps::size() const {
    return count_;
}

const PenStep& PenSteps::operator[](size_t index) const {
    return items_[index];
}

const PenStep* PenSteps::begin() const {
    return items_.data();
}

const PenStep* PenSteps::end() const {
    return items_.data() + count_;
}

PenSteps PenState::onSample(const PenSample& sample) {
    PenSteps   out;
    const bool eraser = (sample.buttons & BUTTON_ERASER) != 0;

    switch (sample.type) {
        case PACKET_TYPE_HOVER:
            release(out); // UP was lost
            ensureTool(out, eraser);
            out.push({PenAction::Hover, PenButton::Left, eraser_});
            break;
        case PACKET_TYPE_DOWN:
            release(out); // previous UP was lost
            ensureTool(out, eraser);
            press(out, sample.buttons);
            break;
        case PACKET_TYPE_MOVE:
            if (down_) {
                out.push({PenAction::Drag, button_, eraser_});
            } else { // DOWN was lost, or the watchdog released the stroke
                ensureTool(out, eraser);
                press(out, sample.buttons);
            }
            break;
        case PACKET_TYPE_UP:
            release(out);
            break;
        default:
            break;
    }
    return out;
}

PenSteps PenState::onStreamLost() {
    PenSteps out;
    release(out);
    if (near_) {
        out.push({PenAction::ProximityLeave, PenButton::Left, eraser_});
        near_ = false;
    }
    return out;
}

bool PenState::isDown() const {
    return down_;
}

bool PenState::inProximity() const {
    return near_;
}

void PenState::release(PenSteps& out) {
    if (down_) {
        out.push({PenAction::Release, button_, eraser_});
        down_ = false;
    }
}

void PenState::press(PenSteps& out, uint8_t buttons) {
    button_ = (buttons & BUTTON_PRIMARY) != 0 ? PenButton::Right : PenButton::Left;
    down_   = true;
    out.push({PenAction::Press, button_, eraser_});
}

void PenState::ensureTool(PenSteps& out, bool eraser) {
    if (near_ && eraser_ == eraser) {
        return;
    }
    if (near_) {
        out.push({PenAction::ProximityLeave, PenButton::Left, eraser_});
    }
    near_   = true;
    eraser_ = eraser;
    out.push({PenAction::ProximityEnter, PenButton::Left, eraser_});
}

} // namespace stylus_cast
