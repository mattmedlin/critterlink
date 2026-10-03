#include "critterlink/sif.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace critterlink {
namespace {
constexpr std::uint32_t ee_start = 0x100, iop_start = 0x01000000;
constexpr std::uint32_t iop_masks = 0x000c0000, iop_flags = 0x0c000000;
void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
bool ee_active(const SifEeDmaState& channel) { return (channel.chcr & ee_start) != 0; }
bool iop_active(const SifIopDmaState& channel) { return (channel.chcr & iop_start) != 0; }
void valid_range(std::span<std::uint8_t> ram, std::uint32_t address) {
    require(address <= ram.size() && ram.size() - address >= 16,
            "SIF DMA address is outside RAM");
}
void ee_write(SifEeDmaState& channel, std::uint32_t offset, std::uint32_t value,
              bool send) {
    if (offset == 0) {
        require(!ee_active(channel) || value == 0, "SIF EE DMA is already active");
        require(value == 0 || value == (send ? 0x101u : 0x100u),
                "unsupported SIF EE DMA mode (only normal RAM transfers)");
        channel.chcr = value;
        return;
    }
    require(!ee_active(channel), "cannot change an active SIF EE DMA channel");
    if (offset == 0x10) {
        require((value & 0xf) == 0 && value < 0x80000000u,
                "SIF EE DMA address must be aligned main RAM");
        channel.address = value;
    } else if (offset == 0x20) {
        require(value <= 0xffff, "SIF EE DMA QWC exceeds 16 bits");
        channel.qwords = value;
    } else {
        throw std::invalid_argument("unsupported SIF EE DMA register");
    }
}
std::uint32_t ee_read(const SifEeDmaState& channel, std::uint32_t offset) {
    if (offset == 0) return channel.chcr;
    if (offset == 0x10) return channel.address;
    if (offset == 0x20) return channel.qwords;
    throw std::invalid_argument("unsupported SIF EE DMA register");
}
void iop_write(SifIopDmaState& channel, std::uint32_t offset, std::uint32_t value,
               bool send) {
    if (offset == 8) {
        require(!iop_active(channel) || value == 0, "SIF IOP DMA is already active");
        require(value == 0 || value == (send ? 0x01000201u : 0x01000200u),
                "unsupported SIF IOP DMA mode (only block transfers)");
        if (value != 0) {
            require((channel.blocks & 0xffff) == 32 && (channel.blocks >> 16) != 0,
                    "SIF IOP DMA requires nonzero 32-word blocks");
        }
        channel.chcr = value;
        channel.block_words = 0;
        return;
    }
    require(!iop_active(channel), "cannot change an active SIF IOP DMA channel");
    if (offset == 0) {
        require((value & 0xf) == 0 && value <= 0x00ffffff,
                "SIF IOP DMA subset requires qword-aligned 24-bit addresses");
        channel.address = value;
    } else if (offset == 4) {
        require((value & 0xffff) == 32 && (value >> 16) != 0,
                "SIF IOP DMA requires nonzero 32-word blocks");
        channel.blocks = value;
        channel.block_words = 0;
    } else {
        throw std::invalid_argument("unsupported SIF IOP DMA register");
    }
}
std::uint32_t iop_read(const SifIopDmaState& channel, std::uint32_t offset) {
    if (offset == 0) return channel.address;
    if (offset == 4) return channel.blocks;
    if (offset == 8) return channel.chcr;
    throw std::invalid_argument("unsupported SIF IOP DMA register");
}
void ee_progress(SifEeDmaState& channel, SifState& state, std::uint32_t flag) {
    channel.address += 16;
    --channel.qwords;
    if (channel.qwords == 0) {
        channel.chcr &= ~ee_start;
        state.ee_completions |= flag;
    }
}
void iop_progress(SifIopDmaState& channel, SifState& state, std::uint32_t mask) {
    channel.address += 16;
    channel.block_words = static_cast<std::uint8_t>(channel.block_words + 4);
    if (channel.block_words == 32) {
        channel.block_words = 0;
        channel.blocks -= 0x10000;
        if ((channel.blocks >> 16) == 0) {
            channel.chcr &= ~iop_start;
            if ((state.iop_dicr2 & mask) != 0) state.iop_dicr2 |= mask << 8;
        }
    }
}
void produce(std::vector<std::array<std::uint8_t, 16>>& fifo,
             std::span<std::uint8_t> ram, std::uint32_t address) {
    valid_range(ram, address);
    std::array<std::uint8_t, 16> packet{};
    std::copy_n(ram.begin() + address, 16, packet.begin());
    fifo.push_back(packet);
}
void consume(std::vector<std::array<std::uint8_t, 16>>& fifo,
             std::span<std::uint8_t> ram, std::uint32_t address) {
    valid_range(ram, address);
    std::copy(fifo.front().begin(), fifo.front().end(), ram.begin() + address);
    fifo.erase(fifo.begin());
}
}

bool Sif::ee_address(std::uint32_t address) noexcept {
    return (address >= 0x1000c000 && address < 0x1000c100) ||
           (address >= 0x1000c400 && address < 0x1000c500) ||
           (address >= 0x1000f200 && address < 0x1000f270);
}
bool Sif::iop_address(std::uint32_t address) noexcept {
    return (address >= 0x1d000000 && address < 0x1d000070) ||
           (address >= 0x1f801520 && address < 0x1f801540) ||
           address == 0x1f801570 || address == 0x1f801574 ||
           address == 0x1f801578 || address == 0x1f8010f4;
}
std::uint32_t Sif::ee_read32(std::uint32_t address) const {
    if (address >= 0x1000c000 && address < 0x1000c100)
        return ee_read(state_.ee_receive, address - 0x1000c000);
    if (address >= 0x1000c400 && address < 0x1000c500)
        return ee_read(state_.ee_send, address - 0x1000c400);
    switch (address) {
    case 0x1000f200: return state_.mscom;
    case 0x1000f210: return state_.smcom;
    case 0x1000f220: return state_.msflag;
    case 0x1000f230: return state_.smflag;
    default: throw std::invalid_argument("unsupported SIF EE register");
    }
}
void Sif::ee_write32(std::uint32_t address, std::uint32_t value) {
    if (address >= 0x1000c000 && address < 0x1000c100) {
        ee_write(state_.ee_receive, address - 0x1000c000, value, false);
        return;
    }
    if (address >= 0x1000c400 && address < 0x1000c500) {
        ee_write(state_.ee_send, address - 0x1000c400, value, true);
        return;
    }
    switch (address) {
    case 0x1000f200: state_.mscom = value; break;
    case 0x1000f220: state_.msflag |= value; break;
    case 0x1000f230: state_.smflag &= ~value; break;
    default: throw std::invalid_argument("unsupported or read-only SIF EE register");
    }
}
std::uint32_t Sif::iop_read32(std::uint32_t address) const {
    if (address >= 0x1f801520 && address < 0x1f801530)
        return iop_read(state_.iop_send, address - 0x1f801520);
    if (address >= 0x1f801530 && address < 0x1f801540)
        return iop_read(state_.iop_receive, address - 0x1f801530);
    switch (address) {
    case 0x1d000000: return state_.mscom;
    case 0x1d000010: return state_.smcom;
    case 0x1d000020: return state_.msflag;
    case 0x1d000030: return state_.smflag;
    case 0x1f801570: return state_.iop_dpcr2;
    case 0x1f801578: return state_.iop_dma_enable;
    case 0x1f8010f4: return state_.iop_dicr | (iop_irq() ? 0x80000000u : 0);
    case 0x1f801574: return state_.iop_dicr2;
    default: throw std::invalid_argument("unsupported SIF IOP register");
    }
}
void Sif::iop_write32(std::uint32_t address, std::uint32_t value) {
    if (address >= 0x1f801520 && address < 0x1f801530) {
        iop_write(state_.iop_send, address - 0x1f801520, value, true);
        return;
    }
    if (address >= 0x1f801530 && address < 0x1f801540) {
        iop_write(state_.iop_receive, address - 0x1f801530, value, false);
        return;
    }
    switch (address) {
    case 0x1d000010: state_.smcom = value; break;
    case 0x1d000020: state_.msflag &= ~value; break;
    case 0x1d000030: state_.smflag |= value; break;
    case 0x1f801570:
        require((value & ~0x0000ff00u) == 0, "unsupported IOP DMA priority channel");
        state_.iop_dpcr2 = value;
        break;
    case 0x1f801578:
        require(value <= 1, "unsupported IOP DMA enable bits");
        state_.iop_dma_enable = value;
        break;
    case 0x1f8010f4:
        require((value & ~0x00800000u) == 0, "unsupported IOP DICR bits");
        state_.iop_dicr = value;
        break;
    case 0x1f801574:
        require((value & ~(iop_masks | iop_flags)) == 0, "unsupported IOP DICR2 bits");
        state_.iop_dicr2 = (state_.iop_dicr2 & iop_flags & ~value) | (value & iop_masks);
        break;
    default: throw std::invalid_argument("unsupported or read-only SIF IOP register");
    }
}

void Sif::tick(std::span<std::uint8_t> ee_ram, std::span<std::uint8_t> iop_ram,
               bool ee_dma_enabled) {
    if (state_.stop) return;
    try {
        // Receivers drain old data first. Newly produced data waits until next tick.
        if (ee_dma_enabled && ee_active(state_.ee_receive)) {
            if (state_.ee_receive.qwords == 0) {
                state_.ee_receive.chcr &= ~ee_start;
                state_.ee_completions |= 0x20;
            } else if (!state_.to_ee.empty()) {
                consume(state_.to_ee, ee_ram, state_.ee_receive.address);
                ee_progress(state_.ee_receive, state_, 0x20);
            }
        }
        const bool iop_send_enabled = state_.iop_dma_enable != 0 &&
                                      (state_.iop_dpcr2 & 0x800) != 0;
        const bool iop_receive_enabled = state_.iop_dma_enable != 0 &&
                                         (state_.iop_dpcr2 & 0x8000) != 0;
        if (iop_receive_enabled && iop_active(state_.iop_receive) && !state_.to_iop.empty()) {
            consume(state_.to_iop, iop_ram, state_.iop_receive.address);
            iop_progress(state_.iop_receive, state_, 0x80000);
        }
        if (iop_send_enabled && iop_active(state_.iop_send) && state_.to_ee.size() < fifo_capacity) {
            produce(state_.to_ee, iop_ram, state_.iop_send.address);
            iop_progress(state_.iop_send, state_, 0x40000);
        }
        if (ee_dma_enabled && ee_active(state_.ee_send)) {
            if (state_.ee_send.qwords == 0) {
                state_.ee_send.chcr &= ~ee_start;
                state_.ee_completions |= 0x40;
            } else if (state_.to_iop.size() < fifo_capacity) {
                produce(state_.to_iop, ee_ram, state_.ee_send.address);
                ee_progress(state_.ee_send, state_, 0x40);
            }
        }
    } catch (const std::invalid_argument& error) {
        state_.stop = error.what();
    }
}
std::uint32_t Sif::take_ee_completions() noexcept {
    return std::exchange(state_.ee_completions, 0);
}
bool Sif::iop_irq() const noexcept {
    const auto pending_enabled = (state_.iop_dicr2 >> 8) & state_.iop_dicr2 & iop_masks;
    return (state_.iop_dicr & 0x00800000) != 0 && pending_enabled != 0;
}
void Sif::restore(const SifState& state) {
    require(state.to_ee.size() <= fifo_capacity && state.to_iop.size() <= fifo_capacity,
            "invalid SIF FIFO snapshot");
    auto valid_ee = [](const SifEeDmaState& channel, bool send) {
        return (channel.chcr == 0 || channel.chcr == (send ? 1u : 0u) ||
                channel.chcr == (send ? 0x101u : 0x100u)) &&
               (channel.address & 0xf) == 0 && channel.address < 0x80000000u &&
               channel.qwords <= 0xffff;
    };
    auto valid_iop = [](const SifIopDmaState& channel, bool send) {
        const auto mode = send ? 0x201u : 0x200u;
        return (channel.chcr == 0 || channel.chcr == mode || channel.chcr == (mode | iop_start)) &&
               (channel.address & 0xf) == 0 && channel.address <= 0x01000000 &&
               channel.block_words < 32 && (channel.block_words % 4) == 0 &&
               (channel.blocks == 0 || (channel.blocks & 0xffff) == 32) &&
               (!iop_active(channel) || (channel.blocks >> 16) != 0) &&
               (iop_active(channel) || channel.block_words == 0) &&
               ((channel.blocks >> 16) != 0 || channel.block_words == 0);
    };
    require(valid_ee(state.ee_receive, false) && valid_ee(state.ee_send, true) &&
            valid_iop(state.iop_receive, false) && valid_iop(state.iop_send, true) &&
            (state.iop_dpcr2 & ~0xff00u) == 0 && state.iop_dma_enable <= 1 &&
            (state.iop_dicr & ~0x00800000u) == 0 &&
            (state.iop_dicr2 & ~(iop_masks | iop_flags)) == 0 &&
            (state.ee_completions & ~0x60u) == 0,
            "invalid SIF DMA snapshot");
    auto replacement = state;
    state_ = std::move(replacement);
}

} // namespace critterlink
