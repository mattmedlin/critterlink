#include "critterlink/integrated_demo.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
void check(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}
template<class F> void rejects(F function) {
    try { function(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid combined snapshot accepted");
}

// Observe output and IOP execution without copying either RAM on every tick.
struct Observation {
    std::uint32_t pc{}, next_pc{};
    std::array<std::uint32_t, 32> registers{};
    std::optional<IopLoad> pending_load;
    bool delay_slot{};
    std::array<std::int16_t, 2> pcm{};
    std::array<std::uint32_t, 64 * 64> framebuffer{};
    std::vector<std::uint8_t> controller_output;
    std::uint16_t controller_read_cursor{};
    bool operator==(const Observation&) const = default;
};

std::vector<Observation> observe(System& system, std::uint64_t ticks,
                                 std::vector<InstructionTrace>& trace) {
    std::vector<Observation> output;
    for (std::uint64_t n = 0; n < ticks; ++n) {
        check(system.run(1, &trace) == RunResult{1, true}, "combined continuation stopped");
        const auto& iop = system.memory().iop().state();
        const auto& hardware = system.memory().hardware();
        const auto& sio = hardware.sio2().state();
        output.push_back({iop.pc, iop.next_pc, iop.gpr, iop.pending_load, iop.delay_slot,
                          hardware.spu().last_sample, hardware.graphics().pixels,
                          sio.output, sio.read_cursor});
    }
    return output;
}
} // namespace

int main() {
    try {
        const auto demo = run_integrated_demo();
        check(demo.ticks == 194 && demo.samples == 194 &&
                  demo.pcm_signature == 15153771150353129381ULL && demo.sprite_pixels == 12 &&
                  demo.concurrent_checkpoint && demo.graphics && demo.audio && demo.input && demo.replay_identical,
              "combined independent output signatures differ");
        const auto timing = integrated_demo_timing();
        check(timing.key_on_tick == 89 && timing.checkpoint_tick == 95 && timing.end_tick == 194,
              "fixture instruction schedule changed without updating expectations");
        System system(integrated_demo_input());
        prepare_integrated_demo(system);
        check(system.memory().hardware().spu().attr == 0 &&
                  system.memory().sio2().state().control == 0 &&
                  system.memory().hardware().graphics().pixels == std::array<std::uint32_t, 4096>{},
              "host prepared peripheral output instead of guest program data");
        check(system.run(timing.checkpoint_tick) == RunResult{95, true}, "combined setup stopped");
        const auto middle = system.state();
        const auto& hardware = middle.memory.hardware;
        check(hardware.dma.qwords == 1 && (hardware.dma.chcr & 0x100) != 0 &&
                  hardware.graphics.vertex_pending && hardware.sio2.active &&
                  hardware.sio2.byte_cursor == 3 && hardware.spu.voices[0].loaded &&
                  hardware.spu.voices[0].envelope > 0 && hardware.spu.voices[0].cursor == 7 &&
                  middle.input_cursor == 1 && middle.input[1].tick == 96,
              "snapshot lacks simultaneous active operations and future input");
        check(system.run(0) == RunResult{0, true} && system.state() == middle,
              "zero budget advanced concurrent operations");
        const auto remaining = timing.end_tick - timing.checkpoint_tick;
        std::vector<InstructionTrace> first_trace, replay_trace;
        const auto first = observe(system, remaining, first_trace);
        const auto completed = system.state();
        system.restore(middle);
        const auto replay = observe(system, remaining, replay_trace);
        check(first == replay && first_trace == replay_trace && system.state() == completed,
              "per-step CPU/framebuffer/PCM/controller replay differs");
        system.restore(middle);
        std::vector<InstructionTrace> partitioned_trace;
        check(system.run(1, &partitioned_trace).budget_exhausted &&
                  system.run(3, &partitioned_trace).budget_exhausted &&
                  system.run(17, &partitioned_trace).budget_exhausted &&
                  system.run(remaining - 21, &partitioned_trace).budget_exhausted,
              "partitioned continuation stopped");
        check(system.state() == completed && partitioned_trace == first_trace,
              "host execution batch sizes changed combined result");
        auto bad = middle;
        bad.memory.hardware.sio2.response[1] ^= 1;
        rejects([&] { system.restore(bad); });
        check(system.state() == completed, "invalid concurrent snapshot changed live state");

        // Change only the pending recorded input. The current packet stays latched;
        // the later guest result changes, proving that replay consumes future input.
        auto different_input = middle;
        different_input.input[1].state.buttons = 0x4000;
        system.restore(different_input);
        check(system.run(remaining).budget_exhausted, "changed-input continuation stopped");
        check(system.memory().iop().read8(0x6004) == 0xbf &&
                  system.memory().iop().read32(0x6010) == 0 &&
                  system.memory().iop().read32(0x6030) == 0,
              "future input was ignored or changed the latched packet");
        check(system.memory().hardware().graphics().pixels == completed.memory.hardware.graphics.pixels &&
                  system.memory().hardware().spu() == completed.memory.hardware.spu,
              "controller input unexpectedly changed independent audio/graphics channels");
        std::cout << "Integrated system tests passed ticks=" << demo.ticks
                  << " signature=" << demo.pcm_signature << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
