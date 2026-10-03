#pragma once

#include "critterlink/graphics.hpp"
#include "critterlink/scheduler.hpp"
#include "critterlink/vector.hpp"
#include "critterlink/iop.hpp"
#include "critterlink/sif.hpp"
#include "critterlink/spu.hpp"
#include "critterlink/sio2.hpp"
#include "critterlink/cdvd.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace critterlink {

struct TimerState {
    std::uint16_t count{}, compare{}, mode{}, phase{};
    bool operator==(const TimerState&) const = default;
};
struct GifDmaState {
    std::uint32_t control{}, status{}, chcr{}, address{}, qwords{};
    bool operator==(const GifDmaState&) const = default;
};
struct VifDmaState {
    std::uint32_t chcr{}, address{}, qwords{};
    std::array<std::uint32_t, 4> pending{};
    std::uint8_t cursor{};
    bool loaded{};
    bool operator==(const VifDmaState&) const = default;
};
struct HardwareState {
    SchedulerState scheduler;
    std::array<TimerState, 4> timers{};
    std::uint32_t interrupt_status{}, interrupt_mask{};
    GifDmaState dma;
    GraphicsState graphics;
    std::optional<std::string> stop;
    VifDmaState vif_dma;
    VectorState vector;
    IopState iop;
    SifState sif;
    SpuState spu;
    Sio2State sio2;
    CdvdState cdvd;
    bool operator==(const HardwareState&) const = default;
};

// Diagnostic logical-bus clock; CPU dispatches the resulting interrupt lines.
class Hardware {
public:
    Hardware();
    std::uint32_t read(std::uint32_t physical_address) const;
    void write(std::uint32_t physical_address, std::uint32_t value);
    void advance(std::uint64_t ticks, std::span<std::uint8_t> ram);
    HardwareState state() const;
    void restore(const HardwareState& state);
    bool int0() const noexcept;
    bool int1() const noexcept;
    std::uint64_t now() const noexcept { return scheduler_.state().now; }
    const std::optional<std::string>& stop() const noexcept { return stop_; }
    const VectorState& vector() const noexcept { return vector_.state(); }
    Iop& iop() noexcept { return iop_; }
    const Iop& iop() const noexcept { return iop_; }
    const SifState& sif() const noexcept { return sif_.state(); }
    const SpuState& spu() const noexcept { return spu_.state(); }
    Sio2& sio2() noexcept { return sio2_; }
    const Sio2& sio2() const noexcept { return sio2_; }
    Cdvd& cdvd() noexcept { return cdvd_; }
    const Cdvd& cdvd() const noexcept { return cdvd_; }
    const GraphicsState& graphics() const noexcept { return graphics_.state(); }

private:
    void tick_timers();
    void tick_vif_dma(std::span<const std::uint8_t> ram);
    void tick_dma(std::span<const std::uint8_t> ram);
    Scheduler scheduler_;
    std::array<TimerState, 4> timers_{};
    std::uint32_t interrupt_status_{}, interrupt_mask_{};
    GifDmaState dma_;
    Graphics graphics_;
    VifDmaState vif_dma_;
    VectorUnit vector_;
    Iop iop_;
    Sif sif_;
    Spu spu_;
    Sio2 sio2_;
    Cdvd cdvd_;
    std::optional<std::string> stop_;
};

} // namespace critterlink
