#pragma once
#include <array>
#include <cstdint>
namespace critterlink {
struct VectorState {
    std::array<std::array<std::uint32_t, 2>, 2048> micro{};
    std::array<std::array<std::uint32_t, 4>, 1024> data{};
    std::array<std::array<std::uint32_t, 4>, 32> vf{{{0, 0, 0, 0x3f800000}}};
    std::array<std::uint16_t, 16> vi{};
    std::uint16_t pc{};
    bool running{}, end_pending{};
    // Payload is 0 (idle), 0x4a (MPG), or 0x6c (UNPACK V4_32).
    std::uint8_t payload{};
    std::uint16_t payload_address{}, payload_remaining{}, payload_lane{};
    bool operator==(const VectorState&) const = default;
};
class VectorUnit {
public:
    bool submit_word(std::uint32_t word);
    void tick();
    bool busy() const noexcept { return state_.running; }
    const VectorState& state() const noexcept { return state_; }
    void restore(const VectorState& state);
private:
    VectorState state_{};
};
} // namespace critterlink
