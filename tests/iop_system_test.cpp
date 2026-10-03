#include "critterlink/iop_demo.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
}
int main() {
    using namespace critterlink;
    try {
        const auto demo = run_iop_demo();
        check(demo.ticks == 223 && demo.response == 40 && demo.exchange_completed &&
            demo.replay_identical, "IOP fixture exact result and restoration");
        System system;
        prepare_iop_demo(system);
        // The host only installs programs and request data; both CPUs program DMA.
        check(system.memory().hardware().sif().ee_send.chcr == 0 &&
            system.memory().hardware().sif().iop_send.chcr == 0, "DMA started by host");
        check(system.run(31) == RunResult{31, true}, "IOP checkpoint execution");
        const auto checkpoint = system.state();
        check(checkpoint.memory.hardware.iop.pending_load.has_value() &&
            checkpoint.memory.hardware.sif.to_iop.size() == 5, "exact FIFO/load checkpoint");
        auto invalid = checkpoint;
        invalid.memory.hardware.iop.gpr[0] = 1;
        bool rejected = false;
        try { system.restore(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && system.state() == checkpoint, "invalid IOP system restore is atomic");
        check(system.run(192) == RunResult{192, true}, "guest exchange execution");
        for (std::uint32_t n = 0; n < 32; ++n) {
            constexpr std::uint32_t request[] = {7, 9, 11, 13};
            check(system.memory().iop().read32(0x1000 + n * 4) == (n < 4 ? request[n] : 0),
                "IOP request payload mismatch");
            check(system.memory().read(0x3000 + n * 4, 4) == (n == 0 ? 40U : 0U),
                "EE response payload mismatch");
        }
        check(system.cpu().state().gpr[11].low == 40 &&
            system.memory().hardware().sif().smcom == 40, "guest mailbox exchange");
        const auto done = system.state();
        check(system.run(0) == RunResult{0, true} && system.state() == done, "zero budget changed IOP");
        System bad;
        bad.memory().iop().write32(0, 0xffffffff);
        bad.memory().iop().start();
        check(bad.run(20).retired == 1 && bad.memory().hardware().stop().has_value(),
            "IOP fault did not stop system explicitly");
        check(bad.memory().hardware().stop()->find("IOP: pc=0x00000000 opcode=0xffffffff") != std::string::npos,
            "IOP stop did not identify the failing guest instruction");
        std::cout << "IOP system tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
