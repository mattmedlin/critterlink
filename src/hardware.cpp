#include "critterlink/hardware.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace critterlink {
namespace {
constexpr std::uint32_t timer_base = 0x10000000;
constexpr std::uint32_t intc_stat = 0x1000f000, intc_mask = 0x1000f010;
constexpr std::uint32_t d2_chcr = 0x1000a000, d2_madr = 0x1000a010, d2_qwc = 0x1000a020;
constexpr std::uint32_t d_ctrl = 0x1000e000, d_stat = 0x1000e010;
constexpr std::uint32_t dma_supported_status = 0x00048004; // channel 2 flag/mask and bus error
unsigned divisor(std::uint16_t mode) { return (mode & 3u) == 0 ? 1u : (mode & 3u) == 1 ? 16u : 256u; }
void require(bool ok, const char* reason) { if (!ok) { throw std::invalid_argument(reason); } }
}

Hardware::Hardware() { scheduler_.schedule(1, EventType::timer); }
bool Hardware::int0() const noexcept { return (interrupt_status_ & interrupt_mask_) != 0; }
bool Hardware::int1() const noexcept { return (dma_.status & (dma_.status >> 16) & 4u) != 0 || (dma_.status & 0x8000u) != 0; }

std::uint32_t Hardware::read(std::uint32_t address) const {
    if (address >= timer_base && address < timer_base + 0x2000) {
        const auto index = (address - timer_base) / 0x800;
        switch ((address - timer_base) % 0x800) {
        case 0: return timers_[index].count;
        case 0x10: return timers_[index].mode;
        case 0x20: return timers_[index].compare;
        default: throw std::invalid_argument("unsupported timer register (HOLD/SBUS is not implemented)");
        }
    }
    switch (address) {
    case intc_stat: return interrupt_status_;
    case intc_mask: return interrupt_mask_;
    case d_ctrl: return dma_.control;
    case d_stat: return dma_.status;
    case d2_chcr: return dma_.chcr;
    case d2_madr: return dma_.address;
    case d2_qwc: return dma_.qwords;
    default: throw std::invalid_argument("unsupported EE hardware register");
    }
}

void Hardware::write(std::uint32_t address, std::uint32_t value) {
    if (address >= timer_base && address < timer_base + 0x2000) {
        auto& timer = timers_[(address - timer_base) / 0x800];
        switch ((address - timer_base) % 0x800) {
        case 0:
            timer.count = static_cast<std::uint16_t>(value);
            timer.phase = 0;
            return;
        case 0x10: {
            require((value & ~0xfc3u) == 0 && (value & 3u) != 3,
                    "timer gate/HBlank modes or reserved MODE bits are unsupported");
            const auto flags = (timer.mode & 0xc00u) & ~(value & 0xc00u);
            if ((timer.mode & 0x83u) != (value & 0x83u)) { timer.phase = 0; }
            timer.mode = static_cast<std::uint16_t>((value & 0x3c3u) | flags);
            return;
        }
        case 0x20: timer.compare = static_cast<std::uint16_t>(value); return;
        default: throw std::invalid_argument("unsupported timer register (HOLD/SBUS is not implemented)");
        }
    }
    switch (address) {
    case intc_stat:
        require((value & ~0x7fffu) == 0, "reserved INTC_STAT bits");
        interrupt_status_ &= ~value;
        return;
    case intc_mask:
        require((value & ~0x7fffu) == 0, "reserved INTC_MASK bits");
        interrupt_mask_ ^= value;
        return;
    case d_ctrl:
        require((value & ~1u) == 0, "only normal DMA enable is implemented");
        dma_.control = value;
        return;
    case d_stat:
        require((value & ~dma_supported_status) == 0, "unsupported DMA interrupt source or mask");
        dma_.status = (dma_.status & ~(value & 0xffffu)) ^ (value & 0xffff0000u);
        return;
    case d2_madr:
        require((dma_.chcr & 0x100u) == 0, "cannot change an active GIF DMA address");
        require((value & 15u) == 0, "GIF DMA address must be qword-aligned; scratchpad mode is unsupported");
        require((value & 0x80000000u) == 0, "GIF DMA scratchpad mode is unsupported");
        dma_.address = value;
        return;
    case d2_qwc:
        require((dma_.chcr & 0x100u) == 0 && value <= 0xffffu, "invalid or active GIF DMA count");
        dma_.qwords = value;
        return;
    case d2_chcr:
        require((value & ~0x101u) == 0 && ((value & 0x100u) == 0 || (value & 1u) != 0),
                "GIF DMA supports only normal RAM-to-GIF transfers (no chain/interleave)");
        require((dma_.chcr & 0x100u) == 0 || value == 0,
                "active GIF DMA may only be stopped, not restarted");
        dma_.chcr = value;
        return;
    default: throw std::invalid_argument("unsupported EE hardware register");
    }
}

void Hardware::tick_timers() {
    for (std::size_t n = 0; n < timers_.size(); ++n) {
        auto& timer = timers_[n];
        if ((timer.mode & 0x80u) == 0) { continue; }
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

void Hardware::tick_dma(std::span<const std::uint8_t> ram) {
    if ((dma_.control & 1u) == 0 || (dma_.chcr & 0x100u) == 0) { return; }
    if (dma_.qwords != 0) {
        if (dma_.address > ram.size() || ram.size() - dma_.address < 16) {
            dma_.chcr &= ~0x100u;
            dma_.status |= 0x8004u;
            stop_ = "GIF DMA source outside RAM; transfer stopped with bus-error status";
            return;
        }
        std::array<std::uint32_t, 4> words{};
        for (unsigned n = 0; n < 16; ++n) {
            words[n / 4] |= std::uint32_t{ram[dma_.address + n]} << ((n % 4) * 8);
        }
        try { graphics_.submit_qword(words); }
        catch (const std::invalid_argument& error) {
            stop_ = std::string("GIF/GS at DMA address ") + std::to_string(dma_.address) + ": " + error.what();
            return;
        }
        dma_.address += 16;
        --dma_.qwords;
    }
    if (dma_.qwords == 0) {
        dma_.chcr &= ~0x100u;
        dma_.status |= 4u;
    }
}

void Hardware::advance(std::uint64_t ticks, std::span<const std::uint8_t> ram) {
    if (stop_) { return; }
    const auto now = scheduler_.state().now;
    if (ticks > std::numeric_limits<std::uint64_t>::max() - now) {
        throw std::overflow_error("hardware logical time overflow");
    }
    const auto target = now + ticks;
    while (auto event = scheduler_.pop_next_until(target)) {
        tick_timers();
        tick_dma(ram);
        if (event->tick != std::numeric_limits<std::uint64_t>::max()) {
            scheduler_.schedule(event->tick + 1, EventType::timer);
        }
        if (stop_) { return; }
    }
}

HardwareState Hardware::state() const {
    return {scheduler_.state(), timers_, interrupt_status_, interrupt_mask_, dma_, graphics_.state(), stop_};
}

void Hardware::restore(const HardwareState& state) {
    Hardware replacement;
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
        require((timer.mode & ~0xfc3u) == 0 && (timer.mode & 3u) != 3 && timer.phase < divisor(timer.mode),
                "invalid timer snapshot");
    }
    require((state.interrupt_status & ~0x7fffu) == 0 && (state.interrupt_mask & ~0x7fffu) == 0,
            "invalid INTC snapshot");
    require((state.dma.control & ~1u) == 0 && (state.dma.status & ~dma_supported_status) == 0 &&
            (state.dma.chcr & ~0x101u) == 0 && (state.dma.address & 15u) == 0 &&
            (state.dma.address & 0x80000000u) == 0 && state.dma.qwords <= 0xffffu &&
            ((state.dma.chcr & 0x100u) == 0 || (state.dma.chcr & 1u) != 0), "invalid DMA snapshot");
    replacement.graphics_.restore(state.graphics);
    replacement.timers_ = state.timers;
    replacement.interrupt_status_ = state.interrupt_status;
    replacement.interrupt_mask_ = state.interrupt_mask;
    replacement.dma_ = state.dma;
    replacement.stop_ = state.stop;
    *this = std::move(replacement);
}

} // namespace critterlink
