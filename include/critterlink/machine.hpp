#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace critterlink {

using Tick = std::uint64_t;

// Host-neutral input sample. Button bits are opaque until pad emulation exists.
struct ControllerState {
    std::uint16_t buttons{};
    std::array<std::uint8_t, 4> axes{128, 128, 128, 128};
    bool operator==(const ControllerState&) const = default;
};

struct InputEvent {
    Tick tick{};
    std::uint8_t port{};
    ControllerState state{};
    bool operator==(const InputEvent&) const = default;
};

// Complete state for the foundation only; no PS2 hardware is implemented yet.
struct MachineState {
    Tick tick{};
    std::array<ControllerState, 2> controllers{};
    bool operator==(const MachineState&) const = default;
};

class Machine {
public:
    // Events must be nondecreasing by tick with valid ports. Equal-tick events
    // execute in supplied order. Owns a copy; callers cannot mutate playback.
    explicit Machine(std::vector<InputEvent> playback = {});
    const MachineState& state() const noexcept;
    void reset() noexcept;
    // Consume events in [current tick, target), leaving target-tick events for
    // the next advance. advance(0) is a no-op. Overflow fails without mutation.
    void advance(Tick ticks);

private:
    MachineState state_{};
    std::vector<InputEvent> playback_;
    std::vector<InputEvent>::size_type next_event_{};
};

} // namespace critterlink
