#include "critterlink/hardware_demo.hpp"

#include <iostream>
#include <limits>
#include <memory>
#include <type_traits>
#include <stdexcept>

namespace {
void check(bool ok, const char* reason) { if (!ok) { throw std::runtime_error(reason); } }
// Construct snapshots directly on the heap, including comparison temporaries,
// so Debug builds fit the Windows default stack without reducing test coverage.
template<class T> auto snapshot(const T& object) {
    using State = std::remove_cvref_t<decltype(object.state())>;
    return std::unique_ptr<State>(new State(object.state()));
}
template<class F> void rejected(F fn) {
    try { fn(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("expected invalid snapshot rejection");
}
}

int main() {
    using namespace critterlink;
    try {
        const auto demo = run_hardware_demo();
        check(demo.ticks == 64 && demo.colored_pixels == 12 && demo.pixel == 0x80402010, "integrated DMA pixels");
        check(demo.timer_interrupt && demo.dma_interrupt && demo.replay_identical, "integrated IRQ/replay");
        check(demo.pad_reply == std::array<std::uint8_t, 5>{0xff, 0x41, 0x5a, 0xff, 0xbf}, "latched pad replay");
        check(demo.audio.samples[0] == 28672 && demo.audio.samples[1] == -32768 &&
              demo.audio.samples[2] == 4096 && demo.audio.samples[3] == -4096, "PCM primitive signature");

        const auto system_storage = std::make_unique<System>(); auto& system = *system_storage;
        prepare_hardware_demo(system);
        check(system.run(24) == RunResult{24, true}, "mid-DMA budget");
        const auto mid_storage = snapshot(system); const auto& mid = *mid_storage;
        check(mid.memory.hardware.dma.qwords == 1 && mid.memory.hardware.graphics.vertex_pending &&
              mid.memory.hardware.graphics.remaining == 1, "snapshot not actually mid-DMA and mid-primitive");
        std::vector<InstructionTrace> first, second;
        system.run(40, &first);
        const auto expected_storage = snapshot(system); const auto& expected = *expected_storage;
        system.restore(mid);
        for (unsigned n = 0; n < 40; ++n) { system.run(1, &second); }
        check(*snapshot(system) == expected && first == second, "partition-independent integrated state/trace");
        check(system.run(0) == RunResult{0, true} && *snapshot(system) == expected, "zero system budget");

        const auto bad_storage = std::make_unique<SystemState>(mid); auto& bad = *bad_storage;
        bad.memory.ram.resize(1);
        rejected([&] { system.restore(bad); });
        check(*snapshot(system) == expected, "bad RAM restore was not atomic");
        bad = mid; bad.memory.hardware.graphics.remaining = 32768;
        rejected([&] { system.restore(bad); });
        check(*snapshot(system) == expected, "bad graphics restore was not atomic");
        bad = mid; bad.pads[1].position = 6;
        rejected([&] { system.restore(bad); });
        check(*snapshot(system) == expected, "bad pad restore was not atomic");
        bad = mid; bad.input = {{0, 0, {}}}; bad.input_cursor = 0;
        rejected([&] { system.restore(bad); });
        check(*snapshot(system) == expected, "bad input cursor restore was not atomic");
        rejected([] { const auto invalid_port = std::make_unique<System>(std::vector<InputEvent>{{0, 2, {}}}); });

        const auto memory_storage = std::make_unique<Memory>(); auto& memory = *memory_storage;
        memory.write(0x10000020, 4, 7);
        check(memory.read(0x90000020, 4) == 7 && memory.read(0xb0000020, 4) == 7, "MMIO aliases");
        bool fault = false;
        try { memory.read(0x10000000, 4, Access::fetch); } catch (const MemoryFault&) { fault = true; }
        check(fault, "executed MMIO as instructions");
        fault = false;
        try { memory.write(0x10000010, 8, 0); } catch (const MemoryFault&) { fault = true; }
        check(fault, "unsupported MMIO width");

        const auto bad_mode_storage = std::make_unique<System>(); auto& bad_mode = *bad_mode_storage;
        // CPU store to reserved timer MODE bit must stop with MMIO diagnostic.
        bad_mode.memory().write(0, 4, 0x3c011000);
        bad_mode.memory().write(4, 4, 0x24021000);
        bad_mode.memory().write(8, 4, 0xac220010);
        check(bad_mode.run(4).retired == 2 && bad_mode.cpu().state().stop.has_value(), "CPU MMIO failure");
        check(bad_mode.cpu().state().stop->diagnostic.find("reserved timer MODE") != std::string::npos &&
              bad_mode.memory().hardware().now() == 2, "failed instruction advanced devices");
        std::cout << "system integration and restoration tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
