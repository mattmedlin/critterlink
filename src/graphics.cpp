#include "critterlink/graphics.hpp"
#include "critterlink/gs_memory.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace critterlink {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
bool register_descriptor(unsigned descriptor) {
    return descriptor == 0 || descriptor == 1 || descriptor == 5 || descriptor == 14 || descriptor == 15;
}
std::uint64_t active_regs(std::uint64_t regs, unsigned count) {
    return count == 16 ? regs : regs & ((std::uint64_t{1} << (count * 4)) - 1);
}
constexpr std::uint64_t bitbltbuf_mask = 0x3f3f3fff3f3f3fffULL;
constexpr std::uint64_t trxpos_mask = 0x1fff07ff07ff07ffULL;
constexpr std::uint64_t trxreg_mask = 0x00000fff00000fffULL;
void validate_rectangle(const GraphicsTransferState& transfer) {
    require(transfer.width >= 1 && transfer.width <= 32 && transfer.base < 16384 &&
            transfer.rect_width != 0 && transfer.rect_width <= 4095 &&
            transfer.rect_height != 0 && transfer.rect_height <= 4095 &&
            (transfer.local_readback || (transfer.rect_width & 1u) == 0) && transfer.x < 2048 && transfer.y < 2048 &&
            transfer.x < transfer.width * 64 &&
            transfer.rect_width <= transfer.width * 64 - transfer.x &&
            transfer.rect_height <= 2048 - transfer.y,
            "GS transfer requires a supported nonzero rectangle within buffer and coordinate bounds");
    // Within each 8x8 PSMCT32 block the local word offset grows with x/y.
    // Check every touched block's last pixel, including base-pointer page carry.
    const auto end_x = transfer.x + transfer.rect_width;
    const auto end_y = transfer.y + transfer.rect_height;
    for (auto y = transfer.y; y < end_y;) {
        const auto next_y = std::min((y / 8 + 1) * 8, end_y);
        for (auto x = transfer.x; x < end_x;) {
            const auto next_x = std::min((x / 8 + 1) * 8, end_x);
            static_cast<void>(gs_psmct32_address(transfer.base, transfer.width, next_x - 1, next_y - 1));
            x = next_x;
        }
        y = next_y;
    }
}
void validate_copy(const GraphicsTransferState& transfer) {
    require(transfer.direction <= 3, "invalid GS copy traversal direction");
    auto source = transfer;
    source.base = transfer.source_base;
    source.width = transfer.source_width;
    source.x = transfer.source_x;
    source.y = transfer.source_y;
    validate_rectangle(source);
    // Exact physical-word intersection, not a coordinate or bounding-range
    // approximation. Rejection is a supported-profile restriction; hardware
    // overlap buffering and collision semantics have not been established.
    std::vector<std::uint64_t> source_words((1U << 20) / 64);
    for (std::uint32_t y = 0; y < transfer.rect_height; ++y) {
        for (std::uint32_t x = 0; x < transfer.rect_width; ++x) {
            const auto word = gs_psmct32_address(source.base, source.width, source.x + x, source.y + y) / 4;
            source_words[word / 64] |= std::uint64_t{1} << (word % 64);
        }
    }
    for (std::uint32_t y = 0; y < transfer.rect_height; ++y) {
        for (std::uint32_t x = 0; x < transfer.rect_width; ++x) {
            const auto word = gs_psmct32_address(transfer.base, transfer.width, transfer.x + x, transfer.y + y) / 4;
            require((source_words[word / 64] & (std::uint64_t{1} << (word % 64))) == 0,
                    "GS local copy physical overlap is unsupported");
        }
    }
}
const std::array<std::uint16_t, 4096>& framebuffer_indices() {
    static const auto indices = [] {
        std::array<std::uint16_t, 4096> result{};
        for (std::uint32_t y = 0; y < 64; ++y) {
            for (std::uint32_t x = 0; x < 64; ++x) {
                result[gs_psmct32_address(0, 1, x, y) / 4] = static_cast<std::uint16_t>(y * 64 + x);
            }
        }
        return result;
    }();
    return indices;
}

}
void Graphics::restore(const GraphicsState& state) {
    require(state.scissor_x0 <= 2047 &&
            state.scissor_x1 <= 2047 && state.scissor_y0 <= 2047 &&
            state.scissor_y1 <= 2047 && (state.offset_x % 16) == 0 &&
            (state.offset_y % 16) == 0 && (state.first_x % 16) == 0 &&
            (state.first_y % 16) == 0 &&
            (!state.vertex_pending || (state.primitive_ready && state.frame_ready)),
            "invalid graphics snapshot");
    require(state.vram.size() == (1U << 20) &&
            (state.mode == GifMode::packed || state.mode == GifMode::image || state.mode == GifMode::reglist) &&
            (state.bitbltbuf & ~bitbltbuf_mask) == 0 && (state.trxpos & ~trxpos_mask) == 0 &&
            (state.trxreg & ~trxreg_mask) == 0 && state.trxdir <= 3,
            "invalid GS local-memory or transfer-register snapshot");
    if ((state.mode == GifMode::reglist || state.mode == GifMode::packed) && state.remaining != 0) {
        require(state.nreg >= 1 && state.nreg <= 16 && state.reg_index < state.nreg &&
                state.remaining <= 32767u * state.nreg - state.reg_index &&
                (state.remaining + state.reg_index) % state.nreg == 0 &&
                (state.mode != GifMode::reglist || (state.nreg & 1u) != 0 || (state.reg_index & 1u) == 0) &&
                state.regs == active_regs(state.regs, state.nreg), "invalid GIF descriptor parser snapshot");
        for (unsigned i = 0; i < state.nreg; ++i) {
            require(register_descriptor(static_cast<unsigned>((state.regs >> (i * 4)) & 15u)),
                    "unsupported GIF snapshot descriptor");
        }
    } else {
        require(state.regs == 0 && state.nreg == 0 && state.reg_index == 0 && state.remaining <= 32767,
                "invalid inactive GIF descriptor metadata");
    }
    require((state.imr & ~0x1f00u) == 0 && (!state.finish_pending || (state.transfer.active && !state.transfer.local_readback)),
            "invalid GS FINISH fence or mask snapshot");
    const auto& transfer = state.transfer;
    if (transfer == GraphicsTransferState{}) {
        require(state.trxdir == 3, "invalid empty GS transfer snapshot");
    } else {
        validate_rectangle(transfer);
        const auto total = transfer.rect_width * transfer.rect_height;
        require(transfer.cursor <= total && transfer.active == (transfer.cursor < total),
                "invalid GS transfer cursor snapshot");
        if (transfer.local_copy) {
            require(state.trxdir == 2 && !transfer.local_readback, "GS copy snapshot has inconsistent transfer direction");
            validate_copy(transfer);
        } else {
            require((transfer.local_readback ? (state.trxdir == 1 && (total & 3u) == 0 &&
                    (transfer.cursor & 3u) == 0) : (state.trxdir == 0 && (transfer.cursor & 1u) == 0)) &&
                    transfer.source_base == 0 &&
                    transfer.source_width == 0 && transfer.source_x == 0 && transfer.source_y == 0 &&
                    transfer.direction == 0, "invalid GS upload or readback snapshot");
        }
    }
    const auto& indices = framebuffer_indices();
    for (std::size_t word = 0; word < indices.size(); ++word) {
        require(state.pixels[indices[word]] == state.vram[word], "GS framebuffer cache differs from local memory");
    }
    // Copy allocation completes before replacing any of the current state.
    auto replacement = state;
    state_ = std::move(replacement);
}
std::uint64_t Graphics::read_privileged(std::uint32_t address) const {
    require(address == 0x12001000u, "unsupported or write-only GS privileged register");
    // Synthetic diagnostic CSR: unimplemented video, FIFO and silicon-ID
    // fields are zero, rather than an invented hardware revision.
    return state_.finish_event ? 2u : 0u;
}
void Graphics::write_privileged(std::uint32_t address, std::uint64_t value) {
    if (address == 0x12001010u) {
        // GS IMR specifies writing ones to undefined bits; retain masks only.
        state_.imr = static_cast<std::uint16_t>(value & 0x1f00u);
        return;
    }
    require(address == 0x12001000u, "unsupported GS privileged register");
    require((value & ~0x00000000fffff002ULL) == 0,
            "unsupported GS CSR event, flush, reset, or reserved control");
    if ((value & 2u) != 0) { state_.finish_event = false; }
}
void Graphics::resolve_finish() noexcept {
    if (state_.finish_pending) {
        state_.finish_pending = false;
        state_.finish_event = true;
    }
}
std::uint32_t Graphics::remaining_readback_qwords() const noexcept {
    const auto& transfer = state_.transfer;
    if (!transfer.local_readback || !transfer.active) { return 0; }
    return (transfer.rect_width * transfer.rect_height - transfer.cursor) / 4;
}
std::array<std::uint32_t, 4> Graphics::produce_readback_qword() {
    auto& transfer = state_.transfer;
    require(remaining_readback_qwords() != 0, "no GS readback qword available");
    std::array<std::uint32_t, 4> words{};
    // Fetch all four words before advancing the producer. Hardware owns the
    // reverse FIFO and latches this result independently of future VRAM writes.
    for (unsigned lane = 0; lane < 4; ++lane) {
        const auto cursor = transfer.cursor + lane;
        const auto x = transfer.x + cursor % transfer.rect_width;
        const auto y = transfer.y + cursor / transfer.rect_width;
        words[lane] = state_.vram[gs_psmct32_address(transfer.base, transfer.width, x, y) / 4];
    }
    transfer.cursor += 4;
    transfer.active = transfer.cursor < transfer.rect_width * transfer.rect_height;
    // Readback consumption does not itself request a FINISH event.
    return words;
}
void Graphics::tick() {
    auto& transfer = state_.transfer;
    if (!transfer.active || !transfer.local_copy) { return; }
    const auto column = transfer.cursor % transfer.rect_width;
    const auto row = transfer.cursor / transfer.rect_width;
    // TRXPOS origins are upper-left, independent of which corner DIR visits
    // first (GS User's Manual v6.0 pp74/133). Reverse visit order, not mapping.
    const auto dx = (transfer.direction & 2u) != 0 ? transfer.rect_width - 1 - column : column;
    const auto dy = (transfer.direction & 1u) != 0 ? transfer.rect_height - 1 - row : row;
    const auto source = gs_psmct32_address(transfer.source_base, transfer.source_width,
                                         transfer.source_x + dx, transfer.source_y + dy);
    const auto destination = gs_psmct32_address(transfer.base, transfer.width,
                                              transfer.x + dx, transfer.y + dy);
    write_vram(destination, state_.vram[source / 4]);
    ++transfer.cursor;
    if (transfer.cursor == transfer.rect_width * transfer.rect_height) {
        transfer.active = false;
        resolve_finish();
    }
}
void Graphics::submit_qword(std::array<std::uint32_t, 4> words) {
    const auto low = std::uint64_t{words[0]} | (std::uint64_t{words[1]} << 32);
    if (state_.remaining == 0) {
        const auto loops = static_cast<std::uint32_t>(low & 0x7fff);
        // EE User's Manual v6.0 p151: a zero-loop tag ignores every field
        // except EOP, which Hardware uses to retain GIF packet ownership.
        if (loops == 0) { state_.gif_eop = (low & 0x8000u) != 0; return; }
        const auto mode = (low >> 58) & 3;
        // The same table specifies FLG3 (Disable) performs the IMAGE operation.
        require((low & 0x00003fffffff0000ULL) == 0, "GIF tag reserved bits must be zero");
        if (mode == 0 || mode == 1) {
            const auto encoded = static_cast<unsigned>((low >> 60) & 15u);
            const auto count = encoded == 0 ? 16u : encoded;
            const auto regs = active_regs(std::uint64_t{words[2]} | (std::uint64_t{words[3]} << 32), count);
            for (unsigned i = 0; i < count; ++i) {
                require(register_descriptor(static_cast<unsigned>((regs >> (i * 4)) & 15u)),
                        "unsupported GIF descriptor");
            }
            // PRE/PRIM are ignored in REGLIST (EE User's Manual v6.0 p159).
            if (mode == 0 && ((low >> 46) & 1) != 0) { write_register(0, (low >> 47) & 0x7ff); }
            state_.mode = mode == 0 ? GifMode::packed : GifMode::reglist;
            state_.remaining = loops * count;
            state_.regs = regs;
            state_.nreg = static_cast<std::uint8_t>(count);
            state_.reg_index = 0;
            state_.gif_eop = (low & 0x8000u) != 0;
            return;
        }
        state_.mode = GifMode::image;
        state_.remaining = loops;
        state_.gif_eop = (low & 0x8000u) != 0;
        return;
    }
    if (state_.mode == GifMode::reglist) {
        // Register writes can render before a later lane rejects. Stage the
        // whole qword even for direct callers; Hardware also stages its FIFO.
        auto staged = *this;
        staged.submit_reglist_payload(words);
        *this = std::move(staged);
        return;
    }
    if (state_.mode == GifMode::image) {
        write_hwreg(low);
        write_hwreg(std::uint64_t{words[2]} | (std::uint64_t{words[3]} << 32));
        --state_.remaining;
        return;
    }
    const auto descriptor = static_cast<std::uint8_t>((state_.regs >> (state_.reg_index * 4)) & 15u);
    switch (descriptor) {
    case 0: write_register(0, words[0] & 0x7ffu); break;
    case 1: {
        const auto rgba = (words[0] & 255u) | ((words[1] & 255u) << 8) |
                          ((words[2] & 255u) << 16) | ((words[3] & 255u) << 24);
        // ST is unsupported, so GIF's tag-initialized Q remains float 1.0.
        // A+D/REGLIST writes to GS RGBAQ do not alter that GIF-internal Q.
        write_register(1, std::uint64_t{rgba} | (std::uint64_t{0x3f800000u} << 32));
        break;
    }
    case 5:
        require((words[3] & 0x8000u) == 0, "PACKED XYZ2 ADC selects unsupported XYZ3");
        write_register(5, std::uint64_t{words[0] & 0xffffu} |
                         (std::uint64_t{words[1] & 0xffffu} << 16) | (std::uint64_t{words[2]} << 32));
        break;
    case 14:
        require((words[2] & 0xffffff00U) == 0 && words[3] == 0, "GIF A+D address must fit eight bits");
        write_register(static_cast<std::uint8_t>(words[2]), low);
        break;
    case 15: break;
    default: throw std::invalid_argument("unsupported PACKED descriptor");
    }
    --state_.remaining;
    state_.reg_index = static_cast<std::uint8_t>((state_.reg_index + 1u) % state_.nreg);
    if (state_.remaining == 0) { state_.regs = 0; state_.nreg = 0; state_.reg_index = 0; }
}
void Graphics::submit_reglist_payload(std::array<std::uint32_t, 4> words) {
    for (unsigned lane = 0; lane < 2 && state_.remaining != 0; ++lane) {
        const auto descriptor = static_cast<std::uint8_t>((state_.regs >> (state_.reg_index * 4)) & 15u);
        const auto value = std::uint64_t{words[lane * 2]} | (std::uint64_t{words[lane * 2 + 1]} << 32);
        if (descriptor != 14 && descriptor != 15) write_register(descriptor, value);
        --state_.remaining;
        state_.reg_index = static_cast<std::uint8_t>((state_.reg_index + 1u) % state_.nreg);
    }
    if (state_.remaining == 0) { state_.regs = 0; state_.nreg = 0; state_.reg_index = 0; }
}
void Graphics::write_vram(std::uint32_t byte_address, std::uint32_t value) {
    const auto word = byte_address / 4;
    state_.vram[word] = value;
    if (word < 4096) { state_.pixels[framebuffer_indices()[word]] = value; }
}
void Graphics::write_hwreg(std::uint64_t value) {
    auto& transfer = state_.transfer;
    if (!transfer.active || transfer.local_copy || transfer.local_readback) { return; }
    for (unsigned half = 0; half < 2; ++half) {
        const auto x = transfer.x + transfer.cursor % transfer.rect_width;
        const auto y = transfer.y + transfer.cursor / transfer.rect_width;
        write_vram(gs_psmct32_address(transfer.base, transfer.width, x, y),
                   static_cast<std::uint32_t>(value >> (half * 32)));
        ++transfer.cursor;
    }
    if (transfer.cursor == transfer.rect_width * transfer.rect_height) {
        transfer.active = false;
        resolve_finish();
    }
}
void Graphics::write_register(std::uint8_t address, std::uint64_t value) {
    switch (address) {
    case 0x00:
        require(value == 6 || value == 0x106, "GS supports only flat untextured context-1 sprites");
        state_.primitive_ready = true;
        state_.vertex_pending = false;
        break;
    case 0x01:
        state_.rgba = static_cast<std::uint32_t>(value);
        state_.rgbaq_q = static_cast<std::uint32_t>(value >> 32);
        break;
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
    case 0x50:
        require((value & ~bitbltbuf_mask) == 0, "GS BITBLTBUF reserved bits");
        state_.bitbltbuf = value;
        break;
    case 0x51:
        require((value & ~trxpos_mask) == 0, "GS TRXPOS reserved bits");
        state_.trxpos = value;
        break;
    case 0x52:
        require((value & ~trxreg_mask) == 0, "GS TRXREG reserved bits");
        state_.trxreg = value;
        break;
    case 0x53: {
        require(value <= 3, "GS TRXDIR reserved bits");
        GraphicsTransferState transfer;
        if (value != 3) {
            transfer.local_readback = value == 1;
            const unsigned shift = transfer.local_readback ? 0u : 32u;
            require(((state_.bitbltbuf >> (shift + 24u)) & 63) == 0,
                    "GS transfer requires PSMCT32 on its active buffer");
            transfer.base = static_cast<std::uint32_t>((state_.bitbltbuf >> shift) & 0x3fff);
            transfer.width = static_cast<std::uint32_t>((state_.bitbltbuf >> (shift + 16u)) & 63);
            transfer.x = static_cast<std::uint32_t>((state_.trxpos >> shift) & 2047);
            transfer.y = static_cast<std::uint32_t>((state_.trxpos >> (shift + 16u)) & 2047);
            transfer.rect_width = static_cast<std::uint32_t>(state_.trxreg & 4095);
            transfer.rect_height = static_cast<std::uint32_t>((state_.trxreg >> 32) & 4095);
            validate_rectangle(transfer);
            require(!transfer.local_readback || ((transfer.rect_width * transfer.rect_height) & 3u) == 0,
                    "GS readback requires whole qwords; padding is unsupported");
            if (value == 2) {
                require(((state_.bitbltbuf >> 24) & 63) == 0, "GS copy requires PSMCT32 source");
                transfer.source_base = static_cast<std::uint32_t>(state_.bitbltbuf & 0x3fff);
                transfer.source_width = static_cast<std::uint32_t>((state_.bitbltbuf >> 16) & 63);
                transfer.source_x = static_cast<std::uint32_t>(state_.trxpos & 2047);
                transfer.source_y = static_cast<std::uint32_t>((state_.trxpos >> 16) & 2047);
                transfer.direction = static_cast<std::uint8_t>((state_.trxpos >> 59) & 3);
                transfer.local_copy = true;
                validate_copy(transfer);
            }
            transfer.active = true;
        }
        // Completion/cancellation resolves the fence for the old operation
        // before replacing it. Invalid starts have already failed above. Copy
        // cancellation and waiting for local copy are explicit model policies.
        resolve_finish();
        state_.transfer = transfer;
        state_.trxdir = static_cast<std::uint8_t>(value);
        break;
    }
    case 0x54: write_hwreg(value); break;
    case 0x61:
        // Always-ready, coalescing FINISH profile; later IMAGE data still flows
        // so an outstanding upload can complete rather than deadlocking GIF.
        if (state_.transfer.active && !state_.transfer.local_readback) { state_.finish_pending = true; }
        else { state_.finish_event = true; }
        break;
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
                const auto location = gs_psmct32_address(0, 1, static_cast<std::uint32_t>(col),
                                                       static_cast<std::uint32_t>(row));
                const auto pixel = state_.vram[location / 4];
                write_vram(location, (pixel & state_.frame_mask) | (state_.rgba & ~state_.frame_mask));
            }
        }
        state_.vertex_pending = false;
        break;
    }
    default: throw std::invalid_argument("unsupported GS register");
    }
}
} // namespace critterlink
