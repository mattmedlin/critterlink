#include "critterlink/cdvd.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace critterlink {
namespace {
constexpr std::uint32_t base = 0x1f402000;
constexpr std::uint32_t dma_start = 0x01000000;
void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
std::uint64_t identity(std::span<const std::uint8_t> media) {
    if (media.empty()) return 0;
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : media) { hash ^= byte; hash *= 1099511628211ULL; }
    return hash;
}
std::uint32_t little32(const std::vector<std::uint8_t>& data, unsigned offset) {
    std::uint32_t value = 0;
    for (unsigned n = 0; n < 4; ++n) value |= std::uint32_t{data[offset + n]} << (n * 8U);
    return value;
}
void finish(CdvdState& state, std::uint8_t error) {
    state.error = error;
    state.irq |= 2;
    state.reading = false;
}
}

bool Cdvd::byte_address(std::uint32_t address) noexcept {
    return address >= base && address < base + 0x40;
}
bool Cdvd::word_address(std::uint32_t address) noexcept {
    return address == 0x1f8010b0 || address == 0x1f8010b4 ||
        address == 0x1f8010b8 || address == 0x1f8010f0;
}
bool Cdvd::address(std::uint32_t address) noexcept {
    return byte_address(address) || word_address(address);
}
void Cdvd::mount(std::vector<std::uint8_t> media) {
    require(!state_.reading && (state_.chcr & dma_start) == 0,
        "cannot change CDVD media during a command or DMA");
    require(media.size() <= max_sectors * sector_size && media.size() % sector_size == 0,
        "CDVD diagnostic media must contain at most eight complete 2048-byte sectors");
    CdvdState replacement;
    replacement.media_id = identity(media);
    replacement.media = std::move(media);
    state_ = std::move(replacement);
}
std::uint8_t Cdvd::read8(std::uint32_t address) const {
    switch (address) {
    case base + 4: return state_.command;
    case base + 5: return state_.reading ? 0x80 : 0x40;
    case base + 6: return state_.error;
    case base + 8: return state_.irq;
    case base + 0x0a: return state_.reading ? 0x06 : state_.media.empty() ? 0 : 0x0a;
    case base + 0x0f: return state_.media.empty() ? 0 : 0x12;
    default: throw std::invalid_argument("unsupported CDVD byte register");
    }
}
void Cdvd::write8(std::uint32_t address, std::uint8_t value) {
    if (address == base + 8) {
        require((value & ~2U) == 0, "unsupported CDVD interrupt acknowledgement");
        state_.irq &= static_cast<std::uint8_t>(~value);
        return;
    }
    require(!state_.reading, "cannot configure CDVD during an active read");
    if (address == base + 5) {
        require(state_.parameters.size() < 11, "CDVD parameter FIFO overflow");
        state_.parameters.push_back(value);
    } else if (address == base + 6) {
        require(value == 0x80, "unsupported CDVD sector transfer format");
        state_.howto = value;
    } else if (address == base + 4) {
        require(value == 6, "unsupported CDVD command (only ReadCD is implemented)");
        state_.command = value;
        state_.error = 0;
        state_.lsn = state_.sectors = state_.transferred = 0;
        if (state_.parameters.size() != 11) {
            state_.parameters.clear();
            finish(state_, 0x22); // SCECdErPRM
            return;
        }
        const auto lsn = little32(state_.parameters, 0);
        const auto sectors = little32(state_.parameters, 4);
        const bool supported = state_.parameters[8] == 0 && state_.parameters[9] == 1 &&
            state_.parameters[10] == 0;
        state_.parameters.clear();
        if (!supported || sectors == 0 || sectors > max_sectors) {
            finish(state_, 0x22);
        } else if (state_.media.empty()) {
            finish(state_, 0x12); // SCECdErNODISC
        } else if (lsn >= state_.media.size() / sector_size ||
            sectors > state_.media.size() / sector_size - lsn) {
            finish(state_, 0x32); // SCECdErEOM
        } else {
            state_.lsn = lsn;
            state_.sectors = sectors;
            state_.reading = true;
        }
    } else {
        throw std::invalid_argument("unsupported or read-only CDVD byte register");
    }
}
std::uint32_t Cdvd::read32(std::uint32_t address) const {
    switch (address) {
    case 0x1f8010b0: return state_.madr;
    case 0x1f8010b4: return state_.bcr;
    case 0x1f8010b8: return state_.chcr;
    case 0x1f8010f0: return state_.dpcr;
    default: throw std::invalid_argument("unsupported CDVD DMA register or access width");
    }
}
void Cdvd::write32(std::uint32_t address, std::uint32_t value) {
    if (address == 0x1f8010f0) {
        require((value & ~0xf000U) == 0, "unsupported primary IOP DMA channel priority");
        state_.dpcr = value;
        return;
    }
    if (address == 0x1f8010b8) {
        require(value == 0 || value == 0x41000200, "unsupported CDVD DMA mode");
        require((state_.chcr & dma_start) == 0 || value == 0, "CDVD DMA is already active");
        state_.chcr = value;
        return;
    }
    require(!state_.reading && (state_.chcr & dma_start) == 0,
        "cannot change CDVD DMA registers while active");
    if (address == 0x1f8010b0) {
        require((value & 3U) == 0 && value < 0x01000000, "invalid CDVD DMA RAM address");
        state_.madr = value;
    } else if (address == 0x1f8010b4) {
        require((value & 0xffffU) == 32 && (value >> 16U) != 0 &&
            (value >> 16U) <= max_sectors * 16, "unsupported CDVD DMA block geometry");
        state_.bcr = value;
    } else {
        throw std::invalid_argument("unsupported CDVD DMA register or access width");
    }
}
void Cdvd::tick(std::span<std::uint8_t> iop_ram) {
    if (state_.stop || !state_.reading || (state_.chcr & dma_start) == 0 ||
        (state_.dpcr & 0x8000U) == 0) return;
    try {
        require(state_.howto == 0x80, "CDVD read requires 2048-byte transfer mode");
        const auto remaining = state_.sectors * sector_size - state_.transferred;
        require((state_.bcr & 0xffffU) == 32 && (state_.bcr >> 16U) == (remaining + 127) / 128,
            "CDVD DMA length does not match read command");
        require(state_.madr <= iop_ram.size() && remaining <= iop_ram.size() - state_.madr,
            "CDVD DMA destination exceeds IOP RAM");
        const auto source = state_.lsn * sector_size + state_.transferred;
        std::copy_n(state_.media.begin() + source, 16, iop_ram.begin() + state_.madr);
        state_.madr += 16;
        state_.transferred += 16;
        if ((state_.transferred & 127U) == 0) state_.bcr -= 0x10000;
        if (state_.transferred == state_.sectors * sector_size) {
            state_.chcr &= ~dma_start;
            finish(state_, 0);
        }
    } catch (const std::invalid_argument& error) {
        state_.stop = error.what();
    }
}
void Cdvd::restore(const CdvdState& state) {
    require(state.media == state_.media && state.media_id == state_.media_id &&
        state.media_id == identity(state.media), "CDVD snapshot media does not match mounted disc");
    require(state.parameters.size() <= 11 && (state.command == 0 || state.command == 6) &&
        (state.error == 0 || state.error == 0x12 || state.error == 0x22 || state.error == 0x32) &&
        (state.irq & ~2U) == 0 && (state.howto == 0 || state.howto == 0x80),
        "invalid CDVD register snapshot");
    require(state.sectors <= max_sectors && state.transferred <= state.sectors * sector_size &&
        (state.transferred & 15U) == 0 &&
        (state.sectors == 0 || (state.lsn < state.media.size() / sector_size &&
            state.sectors <= state.media.size() / sector_size - state.lsn)),
        "invalid CDVD transfer snapshot");
    require(state.sectors != 0 || (state.lsn == 0 && state.transferred == 0),
        "invalid empty CDVD transfer snapshot");
    require(state.reading || state.sectors == 0 ||
        (state.transferred == state.sectors * sector_size && state.command == 6 && state.error == 0),
        "invalid completed CDVD transfer snapshot");
    require(!state.reading || (state.command == 6 && state.error == 0 &&
        state.sectors != 0 && state.transferred < state.sectors * sector_size && state.parameters.empty()),
        "invalid active CDVD command snapshot");
    require((state.madr & 3U) == 0 && state.madr < 0x01000000 &&
        (state.bcr == 0 || ((state.bcr & 0xffffU) == 32 && (state.bcr >> 16U) <= max_sectors * 16)) &&
        (state.chcr == 0 || state.chcr == 0x41000200 || state.chcr == 0x40000200) &&
        (state.dpcr & ~0xf000U) == 0 && (!state.stop || !state.stop->empty()),
        "invalid CDVD DMA snapshot");
    auto replacement = state;
    state_ = std::move(replacement);
}
} // namespace critterlink
