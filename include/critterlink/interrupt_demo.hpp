#pragma once

#include "critterlink/system.hpp"

namespace critterlink {
struct InterruptDemoResult {
    std::uint64_t ticks{}, timer_services{}, dma_services{};
    std::size_t colored_pixels{};
    bool interrupts_acknowledged{}, returned{}, replay_identical{};
};
// Original guest program and handler for the documented diagnostic MMIO subset.
void prepare_interrupt_demo(System& system);
InterruptDemoResult run_interrupt_demo();
} // namespace critterlink
