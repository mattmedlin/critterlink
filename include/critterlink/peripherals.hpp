#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace critterlink {

// Host diagnostic transport, not SIO2 MMIO. Bit 1 means pressed, in PS2 pad order.
struct DigitalPadState {
    std::uint16_t buttons{};
    std::uint16_t latched_buttons{};
    std::uint8_t position{};
    bool selected{};
    bool operator==(const DigitalPadState&) const = default;
};

class DigitalPad {
public:
    const DigitalPadState& state() const noexcept { return state_; }
    void restore(const DigitalPadState& state);
    void set_buttons(std::uint16_t buttons) noexcept { state_.buttons = buttons; }
    // Selection starts a poll and latches host input for a reproducible packet.
    void select() noexcept;
    void deselect() noexcept;
    // Accepts exactly 01 42 00 00 00. Unsupported commands throw, no mutation.
    std::uint8_t exchange(std::uint8_t command);
private:
    DigitalPadState state_{};
};

struct AdpcmBlock {
    std::array<std::int16_t, 28> samples{};
    bool loop_end{};
    bool loop_repeat{};
    bool loop_start{};
    bool operator==(const AdpcmBlock&) const = default;
};

// Stateless, exact filter-zero SPU-format decoding for diagnostic fixtures.
// Other filters/reserved shifts/flags are explicitly unsupported. No playback,
// voice registers, loop execution, mixing, or output timing is implied.
AdpcmBlock decode_adpcm_filter_zero(std::span<const std::uint8_t> bytes);

} // namespace critterlink
