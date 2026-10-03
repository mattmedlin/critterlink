#pragma once
#include "critterlink/system.hpp"
namespace critterlink {
struct IopDemoResult {
    std::uint64_t ticks{};
    std::uint32_t response{};
    bool exchange_completed{}, replay_identical{};
};
void prepare_iop_demo(System& system);
IopDemoResult run_iop_demo();
} // namespace critterlink
