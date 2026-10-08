#include "critterlink/hardware.hpp"
#include "critterlink/dma_chain.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace critterlink {
namespace {
constexpr std::uint32_t timer_base = 0x10000000;
constexpr std::uint32_t intc_stat = 0x1000f000, intc_mask = 0x1000f010;
constexpr std::uint32_t d1_chcr = 0x10009000, d1_madr = 0x10009010, d1_qwc = 0x10009020;
constexpr std::uint32_t d1_tadr = 0x10009030, d1_asr0 = 0x10009040, d1_asr1 = 0x10009050;
constexpr std::uint32_t d2_chcr = 0x1000a000, d2_madr = 0x1000a010, d2_qwc = 0x1000a020;
constexpr std::uint32_t d2_tadr = 0x1000a030, d2_asr0 = 0x1000a040, d2_asr1 = 0x1000a050;
constexpr std::uint32_t d_ctrl = 0x1000e000, d_stat = 0x1000e010;
constexpr std::uint32_t gif_ctrl = 0x10003000, gif_stat = 0x10003020;
constexpr std::uint32_t dma_supported_status = 0x00668066; // channels 1/2/5/6 flags/masks and bus error
bool source_chain_mode(std::uint32_t chcr) { return (chcr & 0xcu) == 4u; }
bool gif_chcr_supported(std::uint32_t chcr) {
    if (!source_chain_mode(chcr)) {
        return (chcr & ~0x101u) == 0 && ((chcr & 0x100u) == 0 || (chcr & 1u) != 0);
    }
    return (chcr & ~0xf00001b5u) == 0 && (chcr & 1u) != 0 && ((chcr >> 4u) & 3u) <= 2;
}
bool aligned_ram_address(std::uint32_t address) { return (address & 0x8000000fu) == 0; }
void clear_gif_packet(GifDmaState& channel, GifChainPhase phase = GifChainPhase::idle) {
    channel.phase = phase;
    channel.next_tag = 0;
    channel.packet_end = false;
    channel.packet_irq = false;
}
void finish_gif_packet(GifDmaState& channel) {
    channel.tag_address = channel.next_tag;
    const bool end = channel.packet_end;
    const bool interrupt = (channel.chcr & 0x80u) != 0 && channel.packet_irq;
    clear_gif_packet(channel, end ? GifChainPhase::idle : GifChainPhase::tag);
    if (end || interrupt) {
        channel.chcr &= ~0x100u;
        channel.status |= 4u;
    }
}
bool vif_chcr_supported(std::uint32_t chcr) {
    return source_chain_mode(chcr) ? gif_chcr_supported(chcr & ~0x40u) : gif_chcr_supported(chcr);
}
void clear_vif_packet(VifDmaState& channel, VifChainPhase phase = VifChainPhase::idle) {
    channel.phase = phase;
    channel.next_tag = 0;
    channel.packet_end = false;
    channel.packet_irq = false;
    channel.tag_words = {};
    channel.tag_cursor = 0;
    channel.pending = {};
    channel.loaded = false;
    channel.cursor = 0;
}
bool finish_vif_packet(VifDmaState& channel) {
    channel.tag_address = channel.next_tag;
    const bool end = channel.packet_end;
    const bool interrupt = (channel.chcr & 0x80u) != 0 && channel.packet_irq;
    clear_vif_packet(channel, end ? VifChainPhase::idle : VifChainPhase::tag);
    if (end || interrupt) { channel.chcr &= ~0x100u; return true; }
    return false;
}
unsigned divisor(std::uint16_t mode) {
    constexpr std::array<unsigned, 4> divisors{1, 16, 256, 1};
    return divisors[mode & 3u];
}
bool effective_timer_gate(std::uint16_t mode) {
    return (mode & 4u) != 0 && !((mode & 3u) == 3 && (mode & 8u) == 0);
}
unsigned timer_gate_mode(std::uint16_t mode) { return (mode >> 4u) & 3u; }
bool timer_running(const TimerState& timer, bool hblank, bool vblank) {
    if ((timer.mode & 0x80u) == 0) { return false; }
    if (!effective_timer_gate(timer.mode)) { return true; }
    if (timer_gate_mode(timer.mode) != 0) { return !timer.gate_wait; }
    return !((timer.mode & 8u) != 0 ? vblank : hblank);
}
void timer_gate_edge(std::array<TimerState, 4>& timers, bool vertical, bool rising) {
    for (auto& timer : timers) {
        if (!effective_timer_gate(timer.mode) || ((timer.mode & 8u) != 0) != vertical) { continue; }
        const auto mode = timer_gate_mode(timer.mode);
        if (mode == 3 || (mode == 1 && rising) || (mode == 2 && !rising)) {
            // Functional policy: selected edges reset even when CUE is clear.
            timer.count = 0;
            timer.phase = 0;
            timer.gate_wait = false;
        }
    }
}
void require(bool ok, const char* reason) { if (!ok) { throw std::invalid_argument(reason); } }
class PeripheralBus final : public IopBus {
public:
    PeripheralBus(Sif& sif, Spu& spu, Sio2& sio2, Cdvd& cdvd, IopIntc& intc, IopTimers& timers, Hardware& hardware, std::span<const std::uint8_t> rom)
        : sif_(sif), spu_(spu), sio2_(sio2), cdvd_(cdvd), intc_(intc), timers_(timers), hardware_(hardware), rom_(rom) {}
    std::uint32_t fetch32(std::uint32_t address) override { return rom_read(address,4); }
    std::uint8_t read8(std::uint32_t address) override {
        if (IopTimers::address(address)) { return static_cast<std::uint8_t>(timers_.read(address, 1)); }
        if (IopIntc::address(address)) { return static_cast<std::uint8_t>(intc_.read(address, 1)); }
        if(rom_address(address)) return static_cast<std::uint8_t>(rom_read(address,1));
        return Sio2::address(address) ? sio2_.read8(address) : cdvd_.read8(address);
    }
    void write8(std::uint32_t address, std::uint8_t value) override {
        if (IopTimers::address(address)) { timers_.write(address, value, 1); return; }
        if (IopIntc::address(address)) { intc_.write(address, value, 1); return; }
        if (Sio2::address(address)) { sio2_.write8(address, value); }
        else { cdvd_.write8(address, value); }
    }
    std::uint16_t read16(std::uint32_t address) override {
        if (IopTimers::address(address)) { return static_cast<std::uint16_t>(timers_.read(address, 2)); }
        return rom_address(address)?static_cast<std::uint16_t>(rom_read(address,2)):spu_.read16(address);
    }
    void write16(std::uint32_t address, std::uint16_t value) override {
        if (IopTimers::address(address)) { timers_.write(address, value, 2); return; }
        spu_.write16(address, value);
    }
    std::uint32_t read32(std::uint32_t address) override {
        if (address == 0x1f801450) { return hardware_.iop_sbus_control(); }
        if (IopTimers::address(address)) { return timers_.read(address); }
        if (IopIntc::address(address)) { return intc_.read(address); }
        if(rom_address(address)) return rom_read(address,4);
        if (Sio2::address(address)) { return sio2_.read32(address); }
        if (Cdvd::word_address(address)) { return cdvd_.read32(address); }
        return sif_.iop_read32(address);
    }
    void write32(std::uint32_t address, std::uint32_t value) override {
        if (address == 0x1f801450) { hardware_.write_iop_sbus_control(value); return; }
        if (IopTimers::address(address)) { timers_.write(address, value); return; }
        if (IopIntc::address(address)) { intc_.write(address, value); return; }
        if (Sio2::address(address)) { sio2_.write32(address, value); }
        else if (Cdvd::word_address(address)) { cdvd_.write32(address, value); }
        else { sif_.iop_write32(address, value); }
    }
private:
    Sif& sif_;
    Spu& spu_;
    Sio2& sio2_;
    Cdvd& cdvd_;
    IopIntc& intc_;
    IopTimers& timers_;
    Hardware& hardware_;
    std::span<const std::uint8_t> rom_;
    static bool rom_address(std::uint32_t address) {return address>=0x1fc00000U && address<0x20000000U;}
    std::uint32_t rom_read(std::uint32_t address,unsigned count) const {
        if(!rom_address(address)) throw std::invalid_argument("IOP instruction fetch outside RAM/ROM");
        const auto offset=address-0x1fc00000U;
        if(offset>=rom_.size() || count>rom_.size()-offset) throw std::invalid_argument("IOP boot ROM bytes are absent");
        std::uint32_t value=0;for(unsigned n=0;n<count;++n)value|=std::uint32_t{rom_[offset+n]}<<(n*8U);
        return value;
    }
};
}

Hardware::Hardware() { scheduler_.schedule(1, EventType::timer); }
bool Hardware::int0() const noexcept { return (interrupt_status_ & interrupt_mask_) != 0; }
bool Hardware::int1() const noexcept { return (dma_.status & (dma_.status >> 16) & 0x66u) != 0 || (dma_.status & 0x8000u) != 0; }

std::uint32_t Hardware::read(std::uint32_t address) const {
    if (Sif::ee_address(address)) { return sif_.ee_read32(address); }
    if (address >= timer_base && address < timer_base + 0x2000) {
        const auto index = (address - timer_base) / 0x800;
        switch ((address - timer_base) % 0x800) {
        case 0: return timers_[index].count;
        case 0x10: return timers_[index].mode;
        case 0x20: return timers_[index].compare;
        case 0x30:
            require(index < 2, "HOLD exists only for timers 0 and 1");
            return timers_[index].hold;
        default: throw std::invalid_argument("unsupported timer register");
        }
    }
    switch (address) {
    case gif_stat: {
        const bool pending = gif_fifo_.count != 0;
        const bool active = !gif_fifo_.paused && (pending || graphics_.state().remaining != 0);
        return (std::uint32_t{gif_fifo_.count} << 24U) | (gif_fifo_.paused ? 8U : 0U) |
               (pending ? 0x40U : 0U) | (active ? 0xe00U : 0U);
    }
    case intc_stat: return interrupt_status_;
    case intc_mask: return interrupt_mask_;
    case d_ctrl: return dma_.control;
    case d_stat: return dma_.status;
    case d1_chcr: return vif_dma_.chcr;
    case d1_madr: return vif_dma_.address;
    case d1_qwc: return vif_dma_.qwords;
    case d1_tadr: return vif_dma_.tag_address;
    case d1_asr0: return vif_dma_.asr[0];
    case d1_asr1: return vif_dma_.asr[1];
    case d2_chcr: return dma_.chcr;
    case d2_madr: return dma_.address;
    case d2_qwc: return dma_.qwords;
    case d2_tadr: return dma_.tag_address;
    case d2_asr0: return dma_.asr[0];
    case d2_asr1: return dma_.asr[1];
    default: throw std::invalid_argument("unsupported EE hardware register");
    }
}

void Hardware::write(std::uint32_t address, std::uint32_t value) {
    if (Sif::ee_address(address)) { sif_.ee_write32(address, value); return; }
    if (address >= timer_base && address < timer_base + 0x2000) {
        auto& timer = timers_[(address - timer_base) / 0x800];
        switch ((address - timer_base) % 0x800) {
        case 0:
            timer.count = static_cast<std::uint16_t>(value);
            timer.phase = 0;
            return;
        case 0x10: {
            require((value & ~0xfffu) == 0, "reserved timer MODE bits are unsupported");
            const auto flags = (timer.mode & 0xc00u) & ~(value & 0xc00u);
            if ((timer.mode & 0x83u) != (value & 0x83u)) { timer.phase = 0; }
            // Only clock/gate configuration changes arm an edge gate. CUE,
            // interrupt enables and flag acknowledgements preserve its start state.
            if ((timer.mode & 0x3fu) != (value & 0x3fu)) {
                const auto mode = static_cast<std::uint16_t>(value);
                timer.gate_wait = effective_timer_gate(mode) && timer_gate_mode(mode) != 0;
            }
            timer.mode = static_cast<std::uint16_t>((value & 0x3ffu) | flags);
            return;
        }
        case 0x20: timer.compare = static_cast<std::uint16_t>(value); return;
        case 0x30:
            require((address - timer_base) / 0x800 < 2, "HOLD exists only for timers 0 and 1");
            timer.hold = static_cast<std::uint16_t>(value); return;
        default: throw std::invalid_argument("unsupported timer register");
        }
    }
    switch (address) {
    case gif_ctrl:
        require((value & ~9U) == 0, "unsupported GIF_CTRL bits");
        if ((value & 1U) != 0) {
            gif_fifo_ = {};
            auto graphics = graphics_.state();
            graphics.remaining = 0; // GIF reset does not reset GS registers or vertices.
            graphics_.restore(graphics);
        }
        gif_fifo_.paused = (value & 8U) != 0;
        return;
    case intc_stat:
        require((value & ~0x7fffu) == 0, "reserved INTC_STAT bits");
        interrupt_status_ &= ~value;
        return;
    case intc_mask:
        require((value & ~0x7fffu) == 0, "reserved INTC_MASK bits");
        interrupt_mask_ ^= value;
        return;
    case d_ctrl:
        require((value & ~1u) == 0, "only global DMA enable is implemented; stall and MFIFO controls are unsupported");
        dma_.control = value;
        return;
    case d_stat:
        require((value & ~dma_supported_status) == 0, "unsupported DMA interrupt source or mask");
        dma_.status = (dma_.status & ~(value & 0xffffu)) ^ (value & 0xffff0000u);
        return;
    case d1_madr:
    case d1_tadr:
    case d1_asr0:
    case d1_asr1:
        require((vif_dma_.chcr & 0x100u) == 0, "cannot change an active VIF1 DMA address");
        require(aligned_ram_address(value), "VIF1 DMA requires qword-aligned RAM; scratchpad unsupported");
        if (address == d1_madr) { vif_dma_.address = value; }
        else if (address == d1_tadr) { vif_dma_.tag_address = value; }
        else { vif_dma_.asr[address == d1_asr0 ? 0 : 1] = value; }
        clear_vif_packet(vif_dma_);
        return;
    case d1_qwc:
        require((vif_dma_.chcr & 0x100u) == 0 && value <= 0xffffu, "invalid or active VIF1 DMA count");
        vif_dma_.qwords = value;
        clear_vif_packet(vif_dma_);
        return;
    case d1_chcr: {
        require(vif_chcr_supported(value), "unsupported VIF1 DMA direction, mode, priority, or stack depth");
        require((vif_dma_.chcr & 0x100u) == 0 || value == 0,
                "active VIF1 DMA may only be aborted, not restarted");
        auto next = vif_dma_;
        if (value == 0 || !source_chain_mode(value)) { clear_vif_packet(next); }
        else {
            const bool resume = next.phase == VifChainPhase::tag && next.qwords == 0 &&
                (value & 0xf0000075u) == (next.chcr & 0xf0000075u);
            if (!resume) {
                clear_vif_packet(next);
                if ((value & 0x100u) != 0) {
                    const auto tag = value & 0xffff0000u;
                    const bool fresh = next.qwords == 0 && (tag == 0 || tag == 0x10000000u);
                    const bool preload = next.qwords != 0 && tag == 0x10000000u;
                    require(fresh || preload, "unsupported VIF1 source-chain launch or restart state");
                    next.phase = fresh ? VifChainPhase::tag : VifChainPhase::payload;
                    if (preload) { next.next_tag = next.tag_address; }
                }
            }
        }
        next.chcr = value;
        vif_dma_ = next;
        return;
    }
    case d2_madr:
    case d2_tadr:
    case d2_asr0:
    case d2_asr1:
        require((dma_.chcr & 0x100u) == 0, "cannot change an active GIF DMA address");
        require(aligned_ram_address(value), "GIF DMA requires qword-aligned RAM; scratchpad unsupported");
        if (address == d2_madr) { dma_.address = value; }
        else if (address == d2_tadr) { dma_.tag_address = value; }
        else { dma_.asr[address == d2_asr0 ? 0 : 1] = value; }
        clear_gif_packet(dma_);
        return;
    case d2_qwc:
        require((dma_.chcr & 0x100u) == 0 && value <= 0xffffu, "invalid or active GIF DMA count");
        dma_.qwords = value;
        clear_gif_packet(dma_);
        return;
    case d2_chcr: {
        require(gif_chcr_supported(value), "unsupported GIF DMA mode, tag transfer, priority, or stack depth");
        require((dma_.chcr & 0x100u) == 0 || value == 0,
                "active GIF DMA may only be aborted, not restarted");
        auto next = dma_;
        // Explicit model abort: retained registers survive, decoded packet does not.
        if (value == 0 || !source_chain_mode(value)) { clear_gif_packet(next); }
        else {
            const bool resume = next.phase == GifChainPhase::tag && next.qwords == 0 &&
                (value & 0xf0000035u) == (next.chcr & 0xf0000035u);
            if (!resume) {
                clear_gif_packet(next);
                if ((value & 0x100u) != 0) {
                    const auto tag = value & 0xffff0000u;
                    const bool fresh = next.qwords == 0 && (tag == 0 || tag == 0x10000000u);
                    const bool preload = next.qwords != 0 && tag == 0x10000000u;
                    require(fresh || preload, "unsupported GIF source-chain launch or restart state");
                    next.phase = fresh ? GifChainPhase::tag : GifChainPhase::payload;
                    if (preload) { next.next_tag = next.tag_address; }
                }
            }
        }
        next.chcr = value;
        dma_ = next;
        return;
    }
    default: throw std::invalid_argument("unsupported EE hardware register");
    }
}

bool Hardware::enqueue_gif(std::array<std::uint32_t, 4> words) {
    if (gif_fifo_.count == 16) return false;
    const auto tail = (unsigned{gif_fifo_.head} + gif_fifo_.count) % 16U;
    gif_fifo_.words[tail] = words;
    ++gif_fifo_.count;
    return true;
}

bool Hardware::write_quadword(std::uint32_t address, std::array<std::uint32_t, 4> words) {
    require(address >= 0x10006000U && address < 0x10007000U && (address & 15U) == 0,
            "quadword MMIO writes support only GIF_FIFO");
    return enqueue_gif(words);
}

void Hardware::tick_gif() {
    if (gif_fifo_.paused || gif_fifo_.count == 0) return;
    // Keep the offending qword and decoder intact on unsupported packet input.
    auto staged = graphics_;
    try { staged.submit_qword(gif_fifo_.words[gif_fifo_.head]); }
    catch (const std::invalid_argument& error) {
        stop_ = std::string("GIF/GS FIFO: ") + error.what();
        return;
    }
    graphics_ = std::move(staged);
    gif_fifo_.words[gif_fifo_.head] = {};
    gif_fifo_.head = static_cast<std::uint8_t>((gif_fifo_.head + 1U) % 16U);
    --gif_fifo_.count;
}

void Hardware::set_sbus_interrupt_line(bool high) noexcept {
    sbus_external_high_ = high;
    sample_sbus_interrupt();
}

void Hardware::write_iop_sbus_control(std::uint32_t value) {
    require((value & ~2U) == 0, "unsupported IOP SBUS control bits");
    iop_sbus_control_ = value;
    sample_sbus_interrupt();
}

void Hardware::sample_sbus_interrupt() noexcept {
    const bool high = sbus_external_high_ && (iop_sbus_control_ & 2U) == 0;
    if (sbus_interrupt_high_ && !high) {
        timers_[0].hold = timers_[0].count;
        timers_[1].hold = timers_[1].count;
        interrupt_status_ |= 2U;
    }
    sbus_interrupt_high_ = high;
}

void Hardware::set_ee_hblank(bool active) noexcept {
    if (ee_hblank_ == active) { return; }
    ee_hblank_ = active;
    timer_gate_edge(timers_, false, active);
    // Functional convention: HBlank rising supplies one external count tick,
    // after processing gates. This input never advances the bus scheduler.
    if (active) { tick_timers(true); }
}

void Hardware::set_ee_vblank(bool active) noexcept {
    if (ee_vblank_ == active) { return; }
    ee_vblank_ = active;
    timer_gate_edge(timers_, true, active);
    interrupt_status_ |= active ? 4u : 8u;
}

void Hardware::tick_timers(bool external_clock) {
    for (std::size_t n = 0; n < timers_.size(); ++n) {
        auto& timer = timers_[n];
        if (((timer.mode & 3u) == 3) != external_clock ||
            !timer_running(timer, ee_hblank_, ee_vblank_)) { continue; }
        ++timer.phase;
        if (timer.phase < divisor(timer.mode)) { continue; }
        timer.phase = 0;
        const bool overflow = timer.count == 0xffffu;
        timer.count = static_cast<std::uint16_t>(timer.count + 1u);
        const bool equal = timer.count == timer.compare;
        if (equal && (timer.mode & 0x100u) != 0 && (timer.mode & 0x400u) == 0) {
            timer.mode |= 0x400u;
            interrupt_status_ |= 1u << (9u + static_cast<unsigned>(n));
        }
        if (overflow && (timer.mode & 0x200u) != 0 && (timer.mode & 0x800u) == 0) {
            timer.mode |= 0x800u;
            interrupt_status_ |= 1u << (9u + static_cast<unsigned>(n));
        }
        if (equal && (timer.mode & 0x40u) != 0) { timer.count = 0; }
    }
}

void Hardware::tick_vif_dma(std::span<const std::uint8_t> ram) {
    auto& channel = vif_dma_;
    if ((dma_.control & 1u) == 0 || (channel.chcr & 0x100u) == 0) { return; }
    const auto bus_error = [&](const char* reason) {
        channel.chcr &= ~0x100u;
        dma_.status |= 0x8002u;
        clear_vif_packet(channel);
        stop_ = reason;
    };
    const auto in_ram = [&](std::uint32_t address) {
        return address <= ram.size() && ram.size() - address >= 16;
    };
    const auto read_qword = [&](std::uint32_t address) {
        std::array<std::uint32_t, 4> words{};
        for (unsigned n = 0; n < 16; ++n) {
            words[n / 4] |= std::uint32_t{ram[address + n]} << ((n % 4) * 8);
        }
        return words;
    };
    const auto finish_packet = [&] {
        if (finish_vif_packet(channel)) { dma_.status |= 2u; }
    };
    if (source_chain_mode(channel.chcr) && channel.phase == VifChainPhase::tag) {
        if (!in_ram(channel.tag_address)) {
            bus_error("VIF1 DMA tag outside RAM; transfer stopped with bus-error status");
            return;
        }
        const auto raw = read_qword(channel.tag_address);
        SourceChainPacket packet;
        try {
            packet = decode_source_chain(channel.tag_address, raw,
                                         {channel.asr, (channel.chcr >> 4u) & 3u});
        } catch (const std::invalid_argument& error) {
            stop_ = std::string("VIF1 DMA source tag: ") + error.what();
            return;
        }
        channel.chcr = (channel.chcr & 0x0000ffcfu) |
                       (std::uint32_t{packet.tag} << 16u) | (packet.stack.depth << 4u);
        channel.asr = packet.stack.addresses;
        channel.qwords = packet.qwords;
        if (packet.stack_overflow) {
            // Functional priority: overflow precedes TTE transport and tag IRQ.
            channel.chcr &= ~0x100u;
            dma_.status |= 2u;
            clear_vif_packet(channel);
            return;
        }
        channel.address = packet.address;
        channel.next_tag = packet.next_tag;
        channel.packet_end = packet.end;
        channel.packet_irq = (packet.tag & 0x8000u) != 0;
        if ((channel.chcr & 0x40u) != 0) {
            channel.tag_words = {raw[2], raw[3]};
            channel.tag_cursor = 0;
            channel.phase = VifChainPhase::tag_words;
        } else {
            channel.phase = VifChainPhase::payload;
            if (channel.qwords == 0) { finish_packet(); }
        }
        return;
    }
    // Physical word indices preserve MPG's 64-bit payload alignment even when
    // the command is transported in the upper half of a DMAtag.
    const auto submit = [&](std::uint32_t word, unsigned physical_word) {
        require(vector_.state().payload != 0 || (word >> 24u) != 0x4au ||
                (physical_word & 1u) != 0, "VIF1 MPG payload requires 64-bit alignment");
        return vector_.submit_word(word);
    };
    if (channel.phase == VifChainPhase::tag_words) {
        try {
            while (channel.tag_cursor < 2) {
                if (!submit(channel.tag_words[channel.tag_cursor], 2 + channel.tag_cursor)) { return; }
                ++channel.tag_cursor;
            }
        } catch (const std::invalid_argument& error) {
            stop_ = std::string("VIF1 at DMA tag address ") + std::to_string(channel.tag_address) + ": " + error.what();
            return;
        }
        channel.tag_words = {};
        channel.tag_cursor = 0;
        channel.phase = VifChainPhase::payload;
        if (channel.qwords == 0) { finish_packet(); }
        return;
    }
    if (channel.qwords != 0) {
        if (!channel.loaded) {
            if (!in_ram(channel.address)) {
                bus_error("VIF1 DMA source outside RAM; transfer stopped with bus-error status");
                return;
            }
            channel.pending = read_qword(channel.address);
            channel.loaded = true;
            channel.cursor = 0;
        }
        try {
            while (channel.cursor < 4) {
                if (!submit(channel.pending[channel.cursor], channel.cursor)) { return; }
                ++channel.cursor;
            }
        } catch (const std::invalid_argument& error) {
            stop_ = std::string("VIF1 at DMA address ") + std::to_string(channel.address) + ": " + error.what();
            return;
        }
        channel.address += 16;
        --channel.qwords;
        channel.loaded = false;
        channel.cursor = 0;
        channel.pending = {};
    }
    if (channel.qwords == 0) {
        if (source_chain_mode(channel.chcr)) { finish_packet(); }
        else { channel.chcr &= ~0x100u; dma_.status |= 2u; }
    }
}

void Hardware::tick_dma(std::span<const std::uint8_t> ram) {
    if ((dma_.control & 1u) == 0 || (dma_.chcr & 0x100u) == 0) { return; }
    const auto bus_error = [&](const char* reason) {
        dma_.chcr &= ~0x100u;
        dma_.status |= 0x8004u;
        clear_gif_packet(dma_);
        stop_ = reason;
    };
    const auto in_ram = [&](std::uint32_t address) {
        return address <= ram.size() && ram.size() - address >= 16;
    };
    const auto read_qword = [&](std::uint32_t address) {
        std::array<std::uint32_t, 4> words{};
        for (unsigned n = 0; n < 16; ++n) {
            words[n / 4] |= std::uint32_t{ram[address + n]} << ((n % 4) * 8);
        }
        return words;
    };
    if (source_chain_mode(dma_.chcr) && dma_.phase == GifChainPhase::tag) {
        if (!in_ram(dma_.tag_address)) {
            bus_error("GIF DMA tag outside RAM; transfer stopped with bus-error status");
            return;
        }
        SourceChainPacket packet;
        try {
            packet = decode_source_chain(dma_.tag_address, read_qword(dma_.tag_address),
                                         {dma_.asr, (dma_.chcr >> 4u) & 3u});
        } catch (const std::invalid_argument& error) {
            // Unsupported profile capabilities are not manufactured bus errors.
            stop_ = std::string("GIF DMA source tag: ") + error.what();
            return;
        }
        dma_.chcr = (dma_.chcr & 0x0000ffcfu) |
                    (std::uint32_t{packet.tag} << 16u) | (packet.stack.depth << 4u);
        dma_.asr = packet.stack.addresses;
        dma_.qwords = packet.qwords;
        if (packet.stack_overflow) {
            dma_.chcr &= ~0x100u;
            dma_.status |= 4u;
            clear_gif_packet(dma_);
            return;
        }
        dma_.address = packet.address;
        dma_.next_tag = packet.next_tag;
        dma_.packet_end = packet.end;
        dma_.packet_irq = (packet.tag & 0x8000u) != 0;
        dma_.phase = GifChainPhase::payload;
        // One tag per logical tick, including empty-tag cycles. No payload is
        // enqueued on this tick, even if the FIFO has room.
        if (dma_.qwords == 0) { finish_gif_packet(dma_); }
        return;
    }
    if (dma_.qwords != 0) {
        if (gif_fifo_.count == 16) { return; }
        if (!in_ram(dma_.address)) {
            bus_error("GIF DMA source outside RAM; transfer stopped with bus-error status");
            return;
        }
        enqueue_gif(read_qword(dma_.address));
        dma_.address += 16;
        --dma_.qwords;
    }
    if (dma_.qwords == 0) {
        if (source_chain_mode(dma_.chcr)) { finish_gif_packet(dma_); }
        else { dma_.chcr &= ~0x100u; dma_.status |= 4u; }
    }
}

void Hardware::advance_iop_pixel_clock(std::uint64_t ticks) {
    if (ticks == 0) { return; }
    iop_intc_.raise_edges(iop_timers_.advance_pixel(ticks));
    sample_iop_interrupts();
}
void Hardware::set_iop_hblank(bool active) {
    iop_intc_.raise_edges(iop_timers_.set_hblank(active));
    sample_iop_interrupts();
}
void Hardware::set_iop_vblank(bool active) {
    iop_intc_.raise_edges(iop_timers_.set_vblank(active));
    sample_iop_interrupts();
}

void Hardware::sample_iop_interrupts() {
    // Sample before/after the guest bus operation and after device service so a
    // falling acknowledgement followed by a new completion in one tick survives.
    const auto levels = (cdvd_.irq() ? 4U : 0U) | (sif_.iop_irq() ? 8U : 0U) |
                        (sio2_.state().interrupt_status != 0 ? 0x20000U : 0U);
    iop_intc_.sample(levels);
    iop_.set_interrupt_line(iop_intc_.irq());
}

void Hardware::advance(std::uint64_t ticks, std::span<std::uint8_t> ram, std::span<const std::uint8_t> boot_rom) {
    if (stop_) { return; }
    const auto now = scheduler_.state().now;
    if (ticks > std::numeric_limits<std::uint64_t>::max() - now) {
        throw std::overflow_error("hardware logical time overflow");
    }
    const auto target = now + ticks;
    while (auto event = scheduler_.pop_next_until(target)) {
        tick_timers();
        // One supplied IOP clock per logical tick until physical clock scheduling.
        iop_intc_.raise_edges(iop_timers_.advance_sysclock(1));
        sample_iop_interrupts();
        PeripheralBus bus(sif_, spu_, sio2_, cdvd_, iop_intc_, iop_timers_, *this, boot_rom);
        iop_.step(&bus);
        sample_iop_interrupts();
        if (iop_.state().stop) { stop_ = "IOP: " + *iop_.state().stop; }
        if (!stop_) {
            sif_.tick(ram, iop_.mutable_ram(), (dma_.control & 1u) != 0);
            dma_.status |= sif_.take_ee_completions();
            if (sif_.state().stop) { stop_ = "SIF: " + *sif_.state().stop; }
        }
        if (!stop_) {
            sio2_.tick();
            if (sio2_.state().stop) { stop_ = "SIO2: " + *sio2_.state().stop; }
        }
        if (!stop_) {
            cdvd_.tick(iop_.mutable_ram());
            if (cdvd_.state().stop) { stop_ = "CDVD: " + *cdvd_.state().stop; }
        }
        if (!stop_) {
            spu_.tick();
            if (spu_.state().stop) { stop_ = "SPU2: " + *spu_.state().stop; }
        }
        try { if (!stop_) { vector_.tick(); } }
        catch (const std::invalid_argument& error) { stop_ = std::string("VU1: ") + error.what(); }
        // Diagnostic scheduling: a GS copy advances before this tick's VIF/GIF
        // delivery, so a newly received TRXDIR begins copying next logical tick.
        // DMAE and GIF pause gate input delivery, not an already active copy.
        if (!stop_) { graphics_.tick(); }
        if (!stop_) { tick_vif_dma(ram); }
        if (!stop_) { tick_dma(ram); }
        if (!stop_) { tick_gif(); }
        sample_iop_interrupts();
        if (event->tick != std::numeric_limits<std::uint64_t>::max()) {
            scheduler_.schedule(event->tick + 1, EventType::timer);
        }
        if (stop_) { return; }
    }
}

HardwareState Hardware::state() const {
    return {scheduler_.state(), timers_, interrupt_status_, interrupt_mask_, dma_, graphics_.state(), stop_, vif_dma_, vector_.state(), iop_.state(), sif_.state(), spu_.state(), sio2_.state(), cdvd_.state(), gif_fifo_, iop_intc_.state(), iop_timers_.state(), sbus_interrupt_high_, sbus_external_high_, iop_sbus_control_, ee_hblank_, ee_vblank_};
}

void Hardware::restore(const HardwareState& state) {
    Hardware replacement = *this;
    replacement.scheduler_.restore(state.scheduler);
    const auto& events = state.scheduler.events;
    const auto now = state.scheduler.now;
    require((now == std::numeric_limits<std::uint64_t>::max() && events.empty()) ||
            (now != std::numeric_limits<std::uint64_t>::max() && events.size() == 1 &&
             events[0].tick == now + 1 && events[0].type == EventType::timer && events[0].payload == 0),
            "hardware snapshot requires exactly one next logical bus event");
    require(events.empty() || events[0].sequence == now, "hardware event sequence must match logical time");
    require(state.scheduler.next_sequence == now + (now != std::numeric_limits<std::uint64_t>::max() ? 1u : 0u),
            "hardware scheduler sequence does not match logical time");
    for (const auto& timer : state.timers) {
        require((timer.mode & ~0xfffu) == 0 && timer.phase < divisor(timer.mode) &&
                (!timer.gate_wait || (effective_timer_gate(timer.mode) && timer_gate_mode(timer.mode) != 0)),
                "invalid timer snapshot");
    }
    require((state.iop_sbus_control & ~2U) == 0 &&
            state.sbus_interrupt_high == (state.sbus_external_high && (state.iop_sbus_control & 2U) == 0),
            "invalid SBUS request snapshot");
    require(state.timers[2].hold == 0 && state.timers[3].hold == 0, "invalid absent timer HOLD register");
    require((state.interrupt_status & ~0x7fffu) == 0 && (state.interrupt_mask & ~0x7fffu) == 0,
            "invalid INTC snapshot");
    const auto& gif = state.dma;
    require((gif.control & ~1u) == 0 && (gif.status & ~dma_supported_status) == 0 &&
            gif_chcr_supported(gif.chcr) && aligned_ram_address(gif.address) && gif.qwords <= 0xffffu &&
            aligned_ram_address(gif.tag_address) && aligned_ram_address(gif.asr[0]) && aligned_ram_address(gif.asr[1]),
            "invalid DMA snapshot");
    const bool metadata_clear = gif.next_tag == 0 && !gif.packet_end && !gif.packet_irq;
    switch (gif.phase) {
    case GifChainPhase::idle:
        require(metadata_clear && (!source_chain_mode(gif.chcr) || (gif.chcr & 0x100u) == 0),
                "invalid idle GIF chain snapshot");
        break;
    case GifChainPhase::tag:
        require(source_chain_mode(gif.chcr) && gif.qwords == 0 && metadata_clear &&
                ((gif.chcr >> 28u) & 7u) != 7 &&
                ((gif.chcr & 0x100u) != 0 ||
                 ((gif.chcr & 0x80000000u) != 0 && ((gif.chcr >> 28u) & 7u) != 0)),
                "invalid GIF tag-fetch snapshot");
        break;
    case GifChainPhase::payload: {
        const auto id = (gif.chcr >> 28u) & 7u;
        require(source_chain_mode(gif.chcr) && (gif.chcr & 0x100u) != 0 && gif.qwords != 0 &&
                aligned_ram_address(gif.next_tag) && gif.packet_irq == ((gif.chcr & 0x80000000u) != 0) &&
                ((id == 0 || id == 7) ? gif.packet_end : (id == 6 || !gif.packet_end)) &&
                (!(id == 6 && gif.packet_end) || (gif.chcr & 0x30u) == 0),
                "invalid GIF packet snapshot");
        break;
    }
    default: throw std::invalid_argument("invalid GIF chain phase");
    }
    const auto& channel = state.vif_dma;
    require(vif_chcr_supported(channel.chcr) && aligned_ram_address(channel.address) &&
            aligned_ram_address(channel.tag_address) && aligned_ram_address(channel.asr[0]) &&
            aligned_ram_address(channel.asr[1]) && channel.qwords <= 0xffffu && channel.cursor < 4 &&
            (!channel.loaded || ((channel.chcr & 0x100u) != 0 && channel.qwords != 0)) &&
            (channel.loaded || (channel.cursor == 0 && channel.pending == std::array<std::uint32_t, 4>{})),
            "invalid VIF1 DMA snapshot");
    const bool vif_metadata_clear = channel.next_tag == 0 && !channel.packet_end && !channel.packet_irq;
    const bool vif_tags_clear = channel.tag_cursor == 0 && channel.tag_words == std::array<std::uint32_t, 2>{};
    switch (channel.phase) {
    case VifChainPhase::idle:
        require(vif_metadata_clear && vif_tags_clear &&
                (!source_chain_mode(channel.chcr) || (channel.chcr & 0x100u) == 0), "invalid idle VIF1 chain snapshot");
        break;
    case VifChainPhase::tag:
        require(source_chain_mode(channel.chcr) && channel.qwords == 0 && vif_metadata_clear && vif_tags_clear &&
                !channel.loaded && ((channel.chcr >> 28u) & 7u) != 7 &&
                ((channel.chcr & 0x100u) != 0 ||
                 ((channel.chcr & 0x80000000u) != 0 && ((channel.chcr >> 28u) & 7u) != 0)),
                "invalid VIF1 tag-fetch snapshot");
        break;
    case VifChainPhase::tag_words:
    case VifChainPhase::payload: {
        const auto id = (channel.chcr >> 28u) & 7u;
        require(source_chain_mode(channel.chcr) && (channel.chcr & 0x100u) != 0 &&
                aligned_ram_address(channel.next_tag) && channel.packet_irq == ((channel.chcr & 0x80000000u) != 0) &&
                ((id == 0 || id == 7) ? channel.packet_end : (id == 6 || !channel.packet_end)) &&
                (!(id == 6 && channel.packet_end) || (channel.chcr & 0x30u) == 0),
                "invalid VIF1 packet snapshot");
        if (channel.phase == VifChainPhase::tag_words) {
            require((channel.chcr & 0x40u) != 0 && channel.tag_cursor < 2 && !channel.loaded,
                    "invalid VIF1 tag transport snapshot");
        } else { require(channel.qwords != 0 && vif_tags_clear, "invalid VIF1 payload snapshot"); }
        break;
    }
    default: throw std::invalid_argument("invalid VIF1 chain phase");
    }
    replacement.iop_.restore(state.iop);
    replacement.iop_intc_.restore(state.iop_intc);
    replacement.iop_timers_.restore(state.iop_timers);
    replacement.sif_.restore(state.sif);
    replacement.spu_.restore(state.spu);
    replacement.sio2_.restore(state.sio2);
    replacement.cdvd_.restore(state.cdvd);
    replacement.vector_.restore(state.vector);
    replacement.vif_dma_ = state.vif_dma;
    replacement.graphics_.restore(state.graphics);
    require(state.gif_fifo.head < 16 && state.gif_fifo.count <= 16, "invalid GIF FIFO snapshot bounds");
    for (unsigned i = state.gif_fifo.count; i < 16; ++i) {
        const auto slot = (unsigned{state.gif_fifo.head} + i) % 16U;
        require(state.gif_fifo.words[slot] == std::array<std::uint32_t, 4>{}, "invalid unused GIF FIFO slot");
    }
    replacement.gif_fifo_ = state.gif_fifo;
    replacement.timers_ = state.timers;
    replacement.sbus_interrupt_high_ = state.sbus_interrupt_high;
    replacement.sbus_external_high_ = state.sbus_external_high;
    replacement.iop_sbus_control_ = state.iop_sbus_control;
    replacement.ee_hblank_ = state.ee_hblank;
    replacement.ee_vblank_ = state.ee_vblank;
    replacement.interrupt_status_ = state.interrupt_status;
    replacement.interrupt_mask_ = state.interrupt_mask;
    replacement.dma_ = state.dma;
    replacement.stop_ = state.stop;
    *this = std::move(replacement);
}

} // namespace critterlink
