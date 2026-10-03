#pragma once

#include "critterlink/system.hpp"

namespace critterlink {

struct IntegratedDemoTiming {
    std::uint64_t key_on_tick{};
    std::uint64_t checkpoint_tick{};
    std::uint64_t end_tick{};
};

struct IntegratedDemoResult {
    std::uint64_t ticks{}, samples{}, pcm_signature{};
    std::size_t sprite_pixels{};
    bool concurrent_checkpoint{}, graphics{}, audio{}, input{}, replay_identical{};
};

// All peripheral commands originate in the loaded EE/IOP programs.
IntegratedDemoTiming integrated_demo_timing();
std::vector<InputEvent> integrated_demo_input();
void prepare_integrated_demo(System& system);
IntegratedDemoResult run_integrated_demo();

} // namespace critterlink
