#include "critterlink/machine.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace critterlink {

Machine::Machine(std::vector<InputEvent> playback) : playback_(std::move(playback)) {
    Tick previous = 0;
    for (const auto& event : playback_) {
        if (event.port >= state_.controllers.size()) {
            throw std::invalid_argument("input port must be 0 or 1");
        }
        if (event.tick < previous) {
            throw std::invalid_argument("input events must be ordered by tick");
        }
        previous = event.tick;
    }
}

const MachineState& Machine::state() const noexcept { return state_; }

void Machine::reset() noexcept {
    state_ = {};
    next_event_ = 0;
}

void Machine::advance(Tick ticks) {
    if (ticks > std::numeric_limits<Tick>::max() - state_.tick) {
        throw std::overflow_error("emulated tick overflow");
    }
    const auto target = state_.tick + ticks;
    while (next_event_ < playback_.size() && playback_[next_event_].tick < target) {
        const auto& event = playback_[next_event_++];
        state_.controllers[event.port] = event.state;
    }
    state_.tick = target;
}

} // namespace critterlink
