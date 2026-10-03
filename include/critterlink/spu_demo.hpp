#pragma once
#include "critterlink/system.hpp"
namespace critterlink {
struct SpuDemoResult {
    std::uint64_t ticks{}, samples{}, signature{};
    bool audible{}, released{}, replay_identical{};
};
void prepare_spu_demo(System& system);
SpuDemoResult run_spu_demo();
} // namespace critterlink
