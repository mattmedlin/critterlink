#pragma once

#include "critterlink/graphics.hpp"
#include "critterlink/scheduler.hpp"

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
struct HardwareState {
    SchedulerState scheduler;
    std::array<TimerState, 4> timers{};
    std::uint32_t interrupt_status{}, interrupt_mask{};
    GifDmaState dma;
    GraphicsState graphics;
    std::optional<std::string> stop;
    bool operator==(const HardwareState&) const = default;
};

// Diagnostic logical-bus clock; no calibrated EE clock or guest IRQ dispatch.
class Hardware {
public:
    Hardware();
    std::uint32_t read(std::uint32_t physical_address) const;
    void write(std::uint32_t physical_address, std::uint32_t value);
    void advance(std::uint64_t ticks, std::span<const std::uint8_t> ram);
    HardwareState state() const;
    void restore(const HardwareState& state);
    bool int0() const noexcept;
    bool int1() const noexcept;
    std::uint64_t now() const noexcept { return scheduler_.state().now; }
    const std::optional<std::string>& stop() const noexcept { return stop_; }
    const GraphicsState& graphics() const noexcept { return graphics_.state(); }

private:
    void tick_timers();
    void tick_dma(std::span<const std::uint8_t> ram);
    Scheduler scheduler_;
    std::array<TimerState, 4> timers_{};
    std::uint32_t interrupt_status_{}, interrupt_mask_{};
    GifDmaState dma_;
    Graphics graphics_;
    std::optional<std::string> stop_;
};

} // namespace critterlink
