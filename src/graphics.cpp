#include "critterlink/graphics.hpp"
#include <algorithm>
#include <stdexcept>

namespace critterlink {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
}
void Graphics::restore(const GraphicsState& state) {
    require(state.remaining <= 32767 && state.scissor_x0 <= 2047 &&
            state.scissor_x1 <= 2047 && state.scissor_y0 <= 2047 &&
            state.scissor_y1 <= 2047 && (state.offset_x % 16) == 0 &&
            (state.offset_y % 16) == 0 && (state.first_x % 16) == 0 &&
            (state.first_y % 16) == 0 &&
            (!state.vertex_pending || (state.primitive_ready && state.frame_ready)),
            "invalid graphics snapshot");
    state_ = state;
}
void Graphics::submit_qword(std::array<std::uint32_t, 4> words) {
    const auto low = std::uint64_t{words[0]} | (std::uint64_t{words[1]} << 32);
    if (state_.remaining == 0) {
        require(((low >> 58) & 3) == 0 && ((low >> 60) & 15) == 1 &&
                words[2] == 14 && words[3] == 0,
                "GIF supports only PACKED single A+D descriptor");
        require((low & 0x00003fffffff0000ULL) == 0,
                "GIF tag reserved bits must be zero");
        const auto loops = static_cast<std::uint32_t>(low & 0x7fff);
        const bool pre = ((low >> 46) & 1) != 0;
        if (pre) write_register(0, (low >> 47) & 0x7ff);
        state_.remaining = loops;
        return;
    }
    require((words[2] & 0xffffff00U) == 0 && words[3] == 0,
            "GIF A+D address must fit eight bits");
    write_register(static_cast<std::uint8_t>(words[2]), low);
    --state_.remaining;
}
void Graphics::write_register(std::uint8_t address, std::uint64_t value) {
    switch (address) {
    case 0x00:
        require(value == 6 || value == 0x106, "GS supports only flat untextured context-1 sprites");
        state_.primitive_ready = true;
        state_.vertex_pending = false;
        break;
    case 0x01: state_.rgba = static_cast<std::uint32_t>(value); break;
    case 0x18:
        require((value & 0xffff0000ffff0000ULL) == 0 &&
                (value & 0x0000000f0000000fULL) == 0,
                "GS diagnostic XYOFFSET must be integral pixels");
        state_.offset_x = static_cast<std::uint16_t>(value);
        state_.offset_y = static_cast<std::uint16_t>(value >> 32);
        break;
    case 0x40:
        require((value & 0xf800f800f800f800ULL) == 0, "GS SCISSOR reserved bits");
        state_.scissor_x0 = static_cast<std::uint16_t>(value & 2047);
        state_.scissor_x1 = static_cast<std::uint16_t>((value >> 16) & 2047);
        state_.scissor_y0 = static_cast<std::uint16_t>((value >> 32) & 2047);
        state_.scissor_y1 = static_cast<std::uint16_t>((value >> 48) & 2047);
        break;
    case 0x4c:
        require(static_cast<std::uint32_t>(value) == 0x10000,
                "GS FRAME requires base 0, width 64, PSMCT32");
        state_.frame_mask = static_cast<std::uint32_t>(value >> 32);
        state_.frame_ready = true;
        break;
    case 0x1a: require(value == 1, "GS requires PRMODECONT=1"); break;
    case 0x47: require(value == 0, "GS diagnostic renderer has no alpha or depth tests"); break;
    case 0x4e: require(value == 0x100000000ULL, "GS requires depth writes masked"); break;
    case 0x7f: break;
    case 0x05: {
        require(state_.primitive_ready && state_.frame_ready, "GS sprite needs PRIM and FRAME");
        const auto x = static_cast<std::uint16_t>(value);
        const auto y = static_cast<std::uint16_t>(value >> 16);
        require(x % 16 == 0 && y % 16 == 0, "GS diagnostic sprite requires integral coordinates");
        if (!state_.vertex_pending) {
            state_.first_x = x; state_.first_y = y; state_.vertex_pending = true;
            break;
        }
        const int x0 = (int{state_.first_x} - int{state_.offset_x}) / 16;
        const int y0 = (int{state_.first_y} - int{state_.offset_y}) / 16;
        const int x1 = (int{x} - int{state_.offset_x}) / 16;
        const int y1 = (int{y} - int{state_.offset_y}) / 16;
        // Integer aligned sprites cover the minimum edge and exclude the maximum.
        const int left = std::max({std::min(x0, x1), int{state_.scissor_x0}, 0});
        const int right = std::min({std::max(x0, x1), int{state_.scissor_x1} + 1, 64});
        const int top = std::max({std::min(y0, y1), int{state_.scissor_y0}, 0});
        const int bottom = std::min({std::max(y0, y1), int{state_.scissor_y1} + 1, 64});
        for (int row = top; row < bottom; ++row) {
            for (int col = left; col < right; ++col) {
                auto& pixel = state_.pixels[static_cast<std::size_t>(row * 64 + col)];
                pixel = (pixel & state_.frame_mask) | (state_.rgba & ~state_.frame_mask);
            }
        }
        state_.vertex_pending = false;
        break;
    }
    default: throw std::invalid_argument("unsupported GS register");
    }
}
} // namespace critterlink
