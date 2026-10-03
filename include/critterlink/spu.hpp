#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace critterlink {

enum class SpuEnvelope : std::uint8_t { off, attack, decay, sustain, release };
struct SpuVoiceState {
    std::uint16_t volume_left{}, volume_right{}, pitch{}, adsr1{}, adsr2{};
    std::uint32_t start{}, loop{}, current{}; // SPU2 halfword addresses.
    std::int32_t envelope{};
    std::uint32_t envelope_counter{};
    SpuEnvelope phase{SpuEnvelope::off};
    std::array<std::int16_t, 28> decoded{};
    std::int16_t history1{}, history2{};
    std::uint8_t cursor{}, flags{};
    bool loaded{};
    bool operator==(const SpuVoiceState&) const = default;
};
struct SpuState {
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(2U * 1024U * 1024U);
    std::array<SpuVoiceState, 2> voices{};
    std::uint16_t attr{}, mmix{}, mix_left{}, mix_right{}, master_left{}, master_right{}, endx{};
    std::uint32_t transfer_address{};
    std::vector<std::uint16_t> transfer_fifo;
    std::uint8_t transfer_cursor{};
    bool transfer_active{};
    std::array<std::int16_t, 2> last_sample{};
    std::uint64_t sample_count{}, signature{14695981039346656037ULL};
    std::optional<std::string> stop;
    bool operator==(const SpuState&) const = default;
};

// Core 0, two voices, manually uploaded ADPCM; diagnostic PCM without interpolation.
class Spu {
public:
    static constexpr std::uint32_t ram_size = 2U * 1024U * 1024U;
    static bool address(std::uint32_t physical) noexcept;
    std::uint16_t read16(std::uint32_t physical) const;
    void write16(std::uint32_t physical, std::uint16_t value);
    // Exactly one logical sample, not one emulated IOP/EE clock cycle.
    void tick();
    const SpuState& state() const noexcept { return state_; }
    void restore(const SpuState& state);
private:
    SpuState state_;
};

} // namespace critterlink
