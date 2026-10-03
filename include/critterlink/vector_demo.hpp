#pragma once

#include "critterlink/system.hpp"

namespace critterlink {
struct VectorDemoResult {
    std::uint64_t ticks{};
    std::array<std::uint32_t, 4> output{};
    bool dma_completed{}, replay_identical{};
};
// Original CPU/VIF/VU1 fixture for the explicitly supported diagnostic subset.
void prepare_vector_demo(System& system);
VectorDemoResult run_vector_demo();
} // namespace critterlink
