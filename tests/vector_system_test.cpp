#include "critterlink/vector_demo.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* reason) { if (!ok) { throw std::runtime_error(reason); } }
}

int main() {
    using namespace critterlink;
    try {
        const auto demo = run_vector_demo();
        check(demo.ticks == 88, "vector fixture logical tick count");
        check(demo.dma_completed && demo.replay_identical, "vector fixture DMA/replay");
        check(demo.output == std::array<std::uint32_t, 4>{7, 9, 11, 13}, "vector fixture exact output");

        System system;
        prepare_vector_demo(system);
        // The first transfer tick has consumed one qword of a larger MPG upload.
        for (unsigned step = 0; step < 64 && system.memory().hardware().state().vif_dma.address != 0x2010; ++step) {
            check(system.run(1) == RunResult{1, true}, "CPU failed before VIF upload checkpoint");
        }
        const auto upload = system.state();
        check(upload.memory.hardware.vif_dma.address == 0x2010 &&
              upload.memory.hardware.vif_dma.qwords != 0 &&
              upload.memory.hardware.vector.payload == 0x4a &&
              upload.memory.hardware.vector.payload_remaining == 24 &&
              upload.memory.hardware.vector.payload_lane == 0, "checkpoint not mid-MPG upload");
        std::vector<InstructionTrace> first, second;
        check(system.run(100, &first) == RunResult{100, true}, "vector first execution stopped");
        const auto complete = system.state();
        check(complete.memory.hardware.vector.data[0] == std::array<std::uint32_t, 4>{7, 9, 11, 13} &&
              complete.memory.hardware.vector.data[1] == std::array<std::uint32_t, 4>{7, 9, 11, 13} &&
              complete.memory.hardware.vector.vi[1] == 1 && !complete.memory.hardware.vector.running,
              "guest DMA/VIF/VU1 did not transfer the independently specified vector");
        system.restore(upload);
        for (unsigned step = 0; step < 100; ++step) {
            check(system.run(1, &second) == RunResult{1, true}, "vector partitioned execution stopped");
        }
        check(system.state() == complete && first == second, "mid-upload full state/trace replay mismatch");
        check(system.run(0) == RunResult{0, true} && system.state() == complete, "zero vector budget changed state");
        std::cout << "vector system and restoration tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
