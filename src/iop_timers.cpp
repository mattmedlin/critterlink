#include "critterlink/iop_timers.hpp"

#include <stdexcept>

namespace critterlink {
namespace {
constexpr std::array<unsigned, 6> irq_bits{4, 5, 6, 14, 15, 16};
struct Register { unsigned timer, offset; };
Register decode(std::uint32_t physical, unsigned width) {
    unsigned timer = 0, offset = 0;
    if (physical >= 0x1f801100 && physical < 0x1f801130) {
        timer = (physical - 0x1f801100) / 16;
        offset = (physical - 0x1f801100) % 16;
    } else if (physical >= 0x1f801480 && physical < 0x1f8014b0) {
        timer = 3 + (physical - 0x1f801480) / 16;
        offset = (physical - 0x1f801480) % 16;
    } else { throw std::invalid_argument("unsupported IOP timer address"); }
    if ((offset != 0 && offset != 4 && offset != 8) ||
        (width != 4 && !(width == 2 && (offset == 4 || timer < 3)))) {
        throw std::invalid_argument("unsupported IOP timer access width or offset");
    }
    return {timer, offset};
}
unsigned divisor(unsigned index, std::uint16_t mode) {
    if (index == 2) { return (mode & 0x200) != 0 ? 8U : 1U; }
    if (index >= 4) {
        constexpr std::array<unsigned, 4> values{1, 8, 16, 256};
        return values[(mode >> 13) & 3];
    }
    return 1;
}
bool blank_gate(unsigned index) { return index == 0 || index == 1 || index == 3; }
bool running(const IopTimersState& state, unsigned index) {
    const auto& timer = state.timers[index];
    if ((timer.mode & 1) == 0) { return true; }
    const unsigned gate = (timer.mode >> 1) & 3;
    if (!blank_gate(index)) { return gate == 1 || gate == 2; }
    const bool blank = index == 0 ? state.hblank : state.vblank;
    if (gate == 0) { return !blank; }
    if (gate == 2) { return blank; }
    return gate != 3 || !timer.gate_wait;
}
void gate_entry(IopTimerState& timer) {
    if ((timer.mode & 1) == 0) { return; }
    const unsigned gate = (timer.mode >> 1) & 3;
    if (gate == 1 || gate == 2) { timer.count = 0; timer.phase = 0; }
    if (gate == 3) { timer.gate_wait = false; }
}
std::uint32_t tick(IopTimersState& state, unsigned index, std::uint64_t clocks) {
    if (!running(state, index) || clocks == 0) { return 0; }
    auto& timer = state.timers[index];
    const unsigned divide = divisor(index, timer.mode);
    const auto remainder = clocks % divide + timer.phase;
    const auto ticks = clocks / divide + remainder / divide;
    timer.phase = static_cast<std::uint16_t>(remainder % divide);
    if (ticks == 0) { return 0; }
    const std::uint64_t modulus = std::uint64_t{1} << (index < 3 ? 16 : 32);
    const auto distance = timer.target > timer.count
        ? std::uint64_t{timer.target} - timer.count
        : modulus - timer.count + timer.target;
    std::uint64_t compares = 0, overflows = 0;
    if ((timer.mode & 8) != 0 && timer.target != 0 && ticks >= distance) {
        compares = 1 + (ticks - distance) / timer.target;
        overflows = timer.count >= timer.target ? 1 : 0;
        timer.count = static_cast<std::uint32_t>((ticks - distance) % timer.target);
    } else {
        compares = ticks / modulus + (ticks % modulus >= distance ? 1U : 0U);
        const auto overflow_distance = modulus - timer.count;
        overflows = ticks / modulus + (ticks % modulus >= overflow_distance ? 1U : 0U);
        timer.count = static_cast<std::uint32_t>((timer.count + ticks % modulus) % modulus);
    }
    if (compares != 0) { timer.mode |= 0x800; }
    if (overflows != 0) { timer.mode |= 0x1000; }
    std::uint64_t events = (timer.mode & 0x10) != 0 ? compares : 0;
    if ((timer.mode & 0x20) != 0 && !((timer.mode & 0x10) != 0 && timer.target == 0)) {
        events += overflows;
    }
    if (events == 0) { return 0; }
    bool request = false;
    const bool armed = (timer.mode & 0x400) != 0;
    if ((timer.mode & 0x40) == 0) {
        request = armed;
        timer.mode &= static_cast<std::uint16_t>(~0x400U);
    } else if ((timer.mode & 0x80) == 0) { request = armed; }
    else {
        request = armed || events >= 2;
        if ((events & 1) != 0) { timer.mode ^= 0x400; }
    }
    return request ? std::uint32_t{1} << irq_bits[index] : 0;
}
}

bool IopTimers::address(std::uint32_t physical) noexcept {
    return (physical >= 0x1f801100 && physical < 0x1f801130) ||
           (physical >= 0x1f801480 && physical < 0x1f8014b0);
}
void IopTimers::restore(IopTimersState state) {
    for (unsigned i = 0; i < state.timers.size(); ++i) {
        const auto& timer = state.timers[i];
        if ((timer.mode & 0x8000) != 0 ||
            (i < 3 && ((timer.count | timer.target) & 0xffff0000) != 0) ||
            timer.phase >= divisor(i, timer.mode) ||
            (timer.gate_wait && (!blank_gate(i) || (timer.mode & 7) != 7))) {
            throw std::invalid_argument("invalid IOP timers snapshot");
        }
    }
    state_ = state;
}
std::uint32_t IopTimers::read(std::uint32_t physical, unsigned width) {
    const auto reg = decode(physical, width);
    auto& timer = state_.timers[reg.timer];
    if (reg.offset == 0) { return timer.count; }
    if (reg.offset == 8) { return timer.target; }
    const auto mode = timer.mode;
    timer.mode &= static_cast<std::uint16_t>(~0x1800U);
    return mode;
}
void IopTimers::write(std::uint32_t physical, std::uint32_t value, unsigned width) {
    const auto reg = decode(physical, width);
    auto& timer = state_.timers[reg.timer];
    if (reg.offset == 4) {
        timer.mode = static_cast<std::uint16_t>((value & 0x63ff) | 0x400);
        timer.count = 0;
        timer.phase = 0;
        timer.gate_wait = blank_gate(reg.timer) && (timer.mode & 7) == 7;
    } else {
        const auto masked = reg.timer < 3 ? value & 0xffff : value;
        if (reg.offset == 0) { timer.count = masked; }
        else {
            timer.target = masked;
            if ((timer.mode & 0x80) == 0) { timer.mode |= 0x400; }
        }
    }
}
std::uint32_t IopTimers::advance_sysclock(std::uint64_t clocks) {
    std::uint32_t requests = 0;
    for (unsigned i = 0; i < state_.timers.size(); ++i) {
        if ((i == 0 || i == 1 || i == 3) && (state_.timers[i].mode & 0x100) != 0) { continue; }
        requests |= tick(state_, i, clocks);
    }
    return requests;
}
std::uint32_t IopTimers::advance_pixel(std::uint64_t clocks) {
    return (state_.timers[0].mode & 0x100) != 0 ? tick(state_, 0, clocks) : 0;
}
std::uint32_t IopTimers::set_hblank(bool level) {
    const bool rising = level && !state_.hblank;
    state_.hblank = level;
    if (!rising) { return 0; }
    gate_entry(state_.timers[0]);
    std::uint32_t requests = 0;
    for (const unsigned i : {1U, 3U}) {
        if ((state_.timers[i].mode & 0x100) != 0) { requests |= tick(state_, i, 1); }
    }
    return requests;
}
std::uint32_t IopTimers::set_vblank(bool level) {
    const bool rising = level && !state_.vblank;
    state_.vblank = level;
    if (rising) { gate_entry(state_.timers[1]); gate_entry(state_.timers[3]); }
    return 0;
}

} // namespace critterlink
