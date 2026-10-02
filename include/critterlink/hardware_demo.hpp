#pragma once

#include "critterlink/system.hpp"

namespace critterlink {

struct HardwareDemoResult {
    std::uint64_t ticks{};
    std::size_t colored_pixels{};
    std::uint32_t pixel{};
    bool timer_interrupt{}, dma_interrupt{}, replay_identical{};
    std::array<std::uint8_t, 5> pad_reply{};
    AdpcmBlock audio;
};

// Host diagnostic fixture. CPU drives timer/INTC/DMA via actual MMIO addresses;
// pad transport and filter-zero decoding remain explicit host-level primitives.
void prepare_hardware_demo(System& system);
HardwareDemoResult run_hardware_demo();

} // namespace critterlink
