#include "critterlink/system.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace critterlink {
namespace {
void validate_input(const std::vector<InputEvent>& input) {
    std::uint64_t previous = 0;
    for (const auto& event : input) {
        if (event.tick < previous || event.port >= 2) {
            throw std::invalid_argument("system input must be ordered controller events for ports 0 or 1");
        }
        previous = event.tick;
    }
}
}

System::System(std::vector<InputEvent> input) : input_(std::move(input)) { validate_input(input_); }
DigitalPad& System::pad(std::size_t port) { return pads_.at(port); }

RunResult System::run(std::uint64_t budget, std::vector<InstructionTrace>* trace) {
    RunResult result;
    std::uint64_t steps = 0;
    while (steps < budget && !cpu_.state().stop && !memory_.hardware().stop()) {
        const auto now = memory_.hardware().now();
        if (now == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("system logical time exhausted");
        }
        if (input_cursor_ < input_.size() && input_[input_cursor_].tick < now) {
            throw std::logic_error("input timeline is behind the device clock; advance through System::run");
        }
        while (input_cursor_ < input_.size() && input_[input_cursor_].tick == now) {
            const auto& event = input_[input_cursor_++];
            pads_[event.port].set_buttons(event.state.buttons);
            memory_.sio2().set_controller(event.port, event.state);
        }
        auto entry = cpu_.step(memory_);
        ++steps;
        if (entry.retired) { ++result.retired; }
        if (entry.retired || entry.exception) { memory_.advance(1); }
        if (trace) { trace->push_back(std::move(entry)); }
    }
    result.budget_exhausted = !cpu_.state().stop && !memory_.hardware().stop() && steps == budget;
    return result;
}

SystemState System::state() const {
    return {cpu_.state(), memory_.state(), {pads_[0].state(), pads_[1].state()}, input_, input_cursor_};
}

void System::restore(const SystemState& state) {
    validate_input(state.input);
    if (state.input_cursor > state.input.size()) { throw std::invalid_argument("invalid input cursor"); }
    const auto now = state.memory.hardware.scheduler.now;
    for (std::size_t n = 0; n < state.input.size(); ++n) {
        if ((n < state.input_cursor && state.input[n].tick > now) ||
            (n >= state.input_cursor && state.input[n].tick < now)) {
            throw std::invalid_argument("input snapshot does not match logical time");
        }
    }
    System replacement(*this);
    replacement.input_ = state.input;
    replacement.memory_.restore(state.memory);
    replacement.cpu_.restore(state.cpu);
    replacement.pads_[0].restore(state.pads[0]);
    replacement.pads_[1].restore(state.pads[1]);
    replacement.input_cursor_ = state.input_cursor;
    *this = std::move(replacement);
}

} // namespace critterlink
