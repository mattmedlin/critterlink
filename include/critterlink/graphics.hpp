#pragma once
#include <array>
#include <cstdint>

namespace critterlink {
// Deliberately bounded diagnostic surface, not GS local-memory emulation.
struct GraphicsState {
    std::array<std::uint32_t, 64 * 64> pixels{};
    std::uint32_t remaining{};
    std::uint32_t rgba{};
    std::uint32_t frame_mask{};
    std::uint16_t offset_x{}, offset_y{};
    std::uint16_t scissor_x0{}, scissor_x1{63}, scissor_y0{}, scissor_y1{63};
    std::uint16_t first_x{}, first_y{};
    bool primitive_ready{}, frame_ready{}, vertex_pending{};
    bool operator==(const GraphicsState&) const = default;
};
class Graphics {
public:
    void submit_qword(std::array<std::uint32_t, 4> words);
    const GraphicsState& state() const noexcept { return state_; }
    void restore(const GraphicsState& state);
    void reset() noexcept { state_ = {}; }
private:
    void write_register(std::uint8_t address, std::uint64_t value);
    GraphicsState state_{};
};
} // namespace critterlink
