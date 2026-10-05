#pragma once

#include <array>
#include <cstdint>

namespace critterlink {

struct IopTimerState {
    std::uint32_t count{}, target{};
    std::uint16_t mode{0x400}, phase{};
    bool gate_wait{};
    bool operator==(const IopTimerState&) const = default;
};
struct IopTimersState {
    std::array<IopTimerState, 6> timers;
    bool hblank{}, vblank{};
    bool operator==(const IopTimersState&) const = default;
};

// Functional clock/edge model. Returned bits are coalesced INTC request events.
// See docs/iop-timers.md for provisional gate/boundary rules and evidence gaps.
class IopTimers {
public:
    static bool address(std::uint32_t physical) noexcept;
    const IopTimersState& state() const noexcept { return state_; }
    void restore(IopTimersState state);
    std::uint32_t read(std::uint32_t physical, unsigned width = 4);
    void write(std::uint32_t physical, std::uint32_t value, unsigned width = 4);
    std::uint32_t advance_sysclock(std::uint64_t clocks);
    std::uint32_t advance_pixel(std::uint64_t clocks);
    std::uint32_t set_hblank(bool level);
    std::uint32_t set_vblank(bool level);
private:
    IopTimersState state_;
};

} // namespace critterlink
