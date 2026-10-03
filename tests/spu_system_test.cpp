#include "critterlink/spu_demo.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
int main() {
    using namespace critterlink;
    try {
        const auto result = run_spu_demo();
        // FNV-1a over 69 silent stereo frames, the 40 fixed oracle frames,
        // release frame 51/51 and 25 silent frames (little-endian int16).
        check(result.ticks == 135 && result.samples == 135 &&
            result.signature == 13041860280187223349ULL &&
            result.audible && result.released && result.replay_identical,
            "register-driven SPU playback/release/restoration");
        System system;
        prepare_spu_demo(system);
        check(system.memory().hardware().spu().attr == 0, "host configured SPU registers");
        for (unsigned n = 0; n < 128 && !system.memory().hardware().spu().transfer_active; ++n)
            check(system.run(1) == RunResult{1, true}, "SPU transfer setup failed");
        const auto transfer = system.state();
        check(transfer.memory.hardware.spu.transfer_active &&
            transfer.memory.hardware.spu.transfer_cursor == 1,
            "checkpoint is not mid manual transfer");
        std::vector<InstructionTrace> first, second;
        check(system.run(150, &first) == RunResult{150, true}, "SPU transfer execution failed");
        const auto expected = system.state();
        check(expected.memory.hardware.spu.ram[0x2000] == 0x1c &&
            expected.memory.hardware.spu.ram[0x2001] == 3 &&
            expected.memory.hardware.spu.ram[0x200f] == 0x77, "guest did not upload sound RAM");
        system.restore(transfer);
        check(system.run(37, &second) == RunResult{37, true} &&
            system.run(113, &second) == RunResult{113, true}, "SPU partitioned replay failed");
        check(system.state() == expected && first == second, "midtransfer replay differs");
        auto bad = expected;
        bad.memory.hardware.spu.ram.pop_back();
        bool rejected = false;
        try { system.restore(bad); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && system.state() == expected, "invalid SPU restore is not atomic");
        check(system.run(0) == RunResult{0, true} && system.state() == expected,
            "zero budget advanced sound");
        System fast;
        prepare_spu_demo(fast);
        bool pitch_patched = false;
        for (std::uint32_t address = 4; address < 0x400; address += 4) {
            if (fast.memory().iop().read32(address) == 0xa4220004 &&
                fast.memory().iop().read32(address - 4) == 0x34021000) {
                fast.memory().iop().write32(address - 4, 0x34022000);
                pitch_patched = true;
                break;
            }
        }
        check(pitch_patched, "test could not locate guest pitch write");
        std::vector<std::int16_t> fast_start;
        for (unsigned n = 0; n < 256 && fast_start.size() < 3; ++n) {
            check(fast.run(1) == RunResult{1, true}, "double-rate SPU fixture stopped");
            if (fast.memory().hardware().spu().voices[0].phase != SpuEnvelope::off)
                fast_start.push_back(fast.memory().hardware().spu().last_sample[0]);
        }
        check(fast_start == std::vector<std::int16_t>{2,16,29},
            "double-rate pitch did not select independently expected samples");
        const auto pitch_checkpoint = fast.state();
        check(pitch_checkpoint.memory.hardware.spu.voices[0].cursor == 6,
            "double-rate pitch checkpoint cursor");
        check(fast.run(64) == RunResult{64, true}, "double-rate continuation stopped");
        const auto pitch_expected = fast.state();
        fast.restore(pitch_checkpoint);
        check(fast.run(17) == RunResult{17, true} && fast.run(47) == RunResult{47, true} &&
            fast.state() == pitch_expected, "double-rate pitch restoration differs");
        const auto bus_fault = [](std::uint32_t opcode, const char* message) {
            System fault;
            fault.memory().iop().write32(0, 0x3c011f90); // LUI r1,SPU base
            fault.memory().iop().write32(4, opcode);
            fault.memory().iop().start();
            check(fault.run(20).retired == 2 && fault.memory().hardware().stop() &&
                fault.memory().iop().state().stop, message);
            const auto stopped = fault.state();
            check(fault.run(20).retired == 0 && fault.state() == stopped,
                "stopped SPU bus fault changed state");
        };
        bus_fault(0xac200000, "word store to halfword SPU register was accepted");
        bus_fault(0xa42007fe, "unmapped SPU halfword register was accepted");
        bus_fault(0xa4200001, "misaligned SPU halfword store was accepted");
        System decoder_fault;
        prepare_spu_demo(decoder_fault);
        bool patched = false;
        for (std::uint32_t address = 0; address < 0x400; address += 4) {
            if (decoder_fault.memory().iop().read32(address) == 0x3402031c) {
                decoder_fault.memory().iop().write32(address, 0x340203fc);
                patched = true; // Guest uploads reserved ADPCM filter 15.
                break;
            }
        }
        check(patched, "test could not locate original ADPCM upload instruction");
        check(decoder_fault.run(200).retired < 200 && decoder_fault.memory().hardware().stop() &&
            decoder_fault.memory().hardware().spu().stop,
            "invalid guest ADPCM did not propagate a SPU diagnostic stop");
        std::cout << "SPU system tests passed ticks=" << result.ticks << " samples=" << result.samples
            << " signature=" << result.signature << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
