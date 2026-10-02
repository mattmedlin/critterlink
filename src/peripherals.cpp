#include "critterlink/peripherals.hpp"

#include <stdexcept>

namespace critterlink {

void DigitalPad::restore(const DigitalPadState& state) {
    if (state.position > 5 || (!state.selected && state.position != 0)) {
        throw std::invalid_argument("invalid digital pad snapshot");
    }
    state_ = state;
}

void DigitalPad::select() noexcept {
    state_.selected = true;
    state_.position = 0;
    state_.latched_buttons = state_.buttons;
}

void DigitalPad::deselect() noexcept {
    state_.selected = false;
    state_.position = 0;
}

std::uint8_t DigitalPad::exchange(std::uint8_t command) {
    if (!state_.selected || state_.position >= 5) {
        throw std::logic_error("digital pad exchange outside selected poll");
    }
    constexpr std::array<std::uint8_t, 5> commands{0x01, 0x42, 0, 0, 0};
    if (command != commands[state_.position]) {
        throw std::invalid_argument("unsupported digital pad command");
    }
    const auto released = static_cast<std::uint16_t>(~state_.latched_buttons);
    const std::array<std::uint8_t, 5> replies{
        0xff, 0x41, 0x5a, static_cast<std::uint8_t>(released & 0xff),
        static_cast<std::uint8_t>(released >> 8)};
    return replies[state_.position++];
}

AdpcmBlock decode_adpcm_filter_zero(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 16) {
        throw std::invalid_argument("SPU ADPCM requires a 16-byte block");
    }
    const auto shift = bytes[0] & 0x0f;
    if ((bytes[0] >> 4) != 0 || shift > 12 || (bytes[1] & 0xf8) != 0) {
        throw std::invalid_argument("unsupported SPU ADPCM filter, shift, or flags");
    }
    AdpcmBlock result{};
    result.loop_end = (bytes[1] & 1) != 0;
    result.loop_repeat = (bytes[1] & 2) != 0;
    result.loop_start = (bytes[1] & 4) != 0;
    const std::int32_t scale = 1 << (12 - shift);
    for (std::size_t i = 0; i < result.samples.size(); ++i) {
        const auto packed = bytes[2 + i / 2];
        const std::int32_t nibble = (packed >> ((i % 2) * 4)) & 0x0f;
        const auto signed_nibble = nibble >= 8 ? nibble - 16 : nibble;
        result.samples[i] = static_cast<std::int16_t>(signed_nibble * scale);
    }
    return result;
}

} // namespace critterlink
