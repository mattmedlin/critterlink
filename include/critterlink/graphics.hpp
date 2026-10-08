#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace critterlink {
enum class GifMode { packed, image };
struct GraphicsTransferState {
    std::uint32_t base{}, width{}, x{}, y{}, rect_width{}, rect_height{}, cursor{};
    bool active{};
    std::uint32_t source_base{}, source_width{}, source_x{}, source_y{};
    std::uint8_t direction{};
    bool local_copy{}, local_readback{};
    bool operator==(const GraphicsTransferState&) const = default;
};
// Fixed 64x64 diagnostic view backed by real PSMCT32 GS local memory.
struct GraphicsState {
    std::array<std::uint32_t, 64 * 64> pixels{};
    std::uint32_t remaining{};
    std::uint32_t rgba{};
    std::uint32_t frame_mask{};
    std::uint16_t offset_x{}, offset_y{};
    std::uint16_t scissor_x0{}, scissor_x1{63}, scissor_y0{}, scissor_y1{63};
    std::uint16_t first_x{}, first_y{};
    bool primitive_ready{}, frame_ready{}, vertex_pending{};
    std::vector<std::uint32_t> vram = std::vector<std::uint32_t>(1U << 20);
    GifMode mode{GifMode::packed};
    std::uint64_t bitbltbuf{}, trxpos{}, trxreg{};
    std::uint8_t trxdir{3};
    GraphicsTransferState transfer;
    bool finish_pending{}, finish_event{};
    std::uint16_t imr{0x1f00};
    bool operator==(const GraphicsState&) const = default;
};
class Graphics {
public:
    // One pixel of an active local copy per diagnostic logical tick.
    void tick();
    bool has_readback() const noexcept { return state_.transfer.local_readback; }
    std::uint32_t remaining_readback_qwords() const noexcept;
    std::array<std::uint32_t, 4> produce_readback_qword();
    std::uint64_t read_privileged(std::uint32_t address) const;
    void write_privileged(std::uint32_t address, std::uint64_t value);
    bool irq() const noexcept { return state_.finish_event && (state_.imr & 0x200u) == 0; }
    void submit_qword(std::array<std::uint32_t, 4> words);
    const GraphicsState& state() const noexcept { return state_; }
    void restore(const GraphicsState& state);
    void reset() { state_ = {}; }
private:
    void write_register(std::uint8_t address, std::uint64_t value);
    void write_vram(std::uint32_t byte_address, std::uint32_t value);
    void write_hwreg(std::uint64_t value);
    void resolve_finish() noexcept;
    GraphicsState state_{};
};
} // namespace critterlink
