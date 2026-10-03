#pragma once

#include "critterlink/system.hpp"

namespace critterlink {
struct IoDemoResult {
    std::uint64_t ticks{};
    bool digital{}, analog{}, card{}, disc{}, replay_identical{};
};
std::vector<InputEvent> io_demo_input();
void prepare_io_demo(System& system);
IoDemoResult run_io_demo();
} // namespace critterlink
