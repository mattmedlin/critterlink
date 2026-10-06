#include "critterlink/system.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace critterlink;

// Factual expected values: ps2autotests IOP LSU delay observations (lsudelay).
// These are independently encoded guests with different register allocation and
// RAM output, not an import of the upstream test implementation. The upstream
// assembler LD pseudo-op observations are deliberately outside this test's scope.
struct Probe {
    const char* name;
    std::array<std::uint32_t, 14> code;
    unsigned load_index;
    std::uint32_t loaded, result;
};
constexpr std::array<Probe, 7> probes{{
    {"LB delay 0", {0x3c0a1122, 0x354a3344, 0x800a1000, 0x340a0000,
                    0x000a5825, 0xac0b1100, 0x1000ffff, 0}, 2, 0x37, 0},
    {"LW delay 0", {0x3c0a1122, 0x354a3344, 0x8c0a1000, 0x340a0000,
                    0x000a5825, 0xac0b1100, 0x1000ffff, 0}, 2, 0x13371337, 0},
    {"LB delay 1", {0x3c0a1122, 0x354a3344, 0x800a1000, 0x000a5825,
                    0xac0b1100, 0x1000ffff, 0}, 2, 0x37, 0x11223344},
    {"LW delay 1", {0x3c0a1122, 0x354a3344, 0x8c0a1000, 0x000a5825,
                    0xac0b1100, 0x1000ffff, 0}, 2, 0x13371337, 0x11223344},
    {"LB delay 2", {0x3c0a1122, 0x354a3344, 0x800a1000, 0,
                    0x000a5825, 0xac0b1100, 0x1000ffff, 0}, 2, 0x37, 0x37},
    {"LW delay 2", {0x3c0a1122, 0x354a3344, 0x8c0a1000, 0,
                    0x000a5825, 0xac0b1100, 0x1000ffff, 0}, 2, 0x13371337, 0x13371337},
    // r12 copies the old r10; BEQ must observe that old value immediately after
    // LW. The taken path stores 2, while a prematurely visible load stores 1.
    {"LW then branch", {0x3c0a1122, 0x354a3344, 0x000a6025, 0x8c0a1000,
                        0x114c0004, 0, 0x340b0001, 0x10000002,
                        0, 0x340b0002, 0xac0b1100, 0x1000ffff, 0}, 3, 0x13371337, 2}
}};
void check(bool value, const Probe& probe, const char* reason) {
    if (!value) { throw std::runtime_error(std::string(probe.name) + ": " + reason); }
}
void install(Iop& iop, const Probe& probe, bool architectural) {
    iop.start();
    auto state = iop.state(); state.architectural_exceptions = architectural;
    iop.restore(state);
    std::uint32_t pc = 0;
    for (const auto word : probe.code) { iop.write32(pc, word); pc += 4; }
    iop.write32(0x1000, 0x13371337);
    iop.write32(0x1100, 0xdeadbeef);
}
void check_pending(const Iop& iop, const Probe& probe) {
    check(iop.state().pc == (probe.load_index + 1) * 4 &&
          iop.state().gpr[10] == 0x11223344 &&
          iop.state().pending_load == IopLoad{10, probe.loaded}, probe,
          "load checkpoint did not retain old register and pending result");
}
void check_result(const Iop& iop, const Probe& probe) {
    check(!iop.state().stop && iop.state().gpr[0] == 0 &&
          iop.state().gpr[11] == probe.result && iop.read32(0x1100) == probe.result &&
          !iop.state().pending_load, probe, "guest result differs from published observation");
}
void direct_observations() {
    for (const bool architectural : {false, true}) for (const auto& probe : probes) {
        Iop iop; install(iop, probe, architectural);
        for (unsigned n = 0; n <= probe.load_index; ++n) {
            check(iop.step(), probe, "setup failed to retire");
        }
        check_pending(iop, probe);
        const auto checkpoint = iop.state();
        for (unsigned n = 0; n < 20; ++n) { check(iop.step(), probe, "continuation failed"); }
        check_result(iop, probe);
        const auto expected = iop.state();
        iop.restore(checkpoint);
        for (unsigned n = 0; n < 20; ++n) { check(iop.step(), probe, "replay failed"); }
        check(iop.state() == expected, probe, "pending-load replay changed complete IOP state");
    }
}
void system_replay() {
    for (const bool architectural : {false, true}) for (const auto& probe : probes) {
        System system;
        auto& iop = system.memory().iop(); install(iop, probe, architectural);
        // Original EE idle loop keeps the full scheduler/device path running.
        system.memory().write(0, 4, 0x1000ffff);
        system.memory().write(4, 4, 0);
        const auto setup = static_cast<std::uint64_t>(probe.load_index) + 1;
        check(system.run(setup) == RunResult{setup, true}, probe, "System setup failed");
        check_pending(iop, probe);
        const auto checkpoint = system.state();
        std::vector<InstructionTrace> first, second;
        check(system.run(20, &first) == RunResult{20, true}, probe, "System continuation failed");
        check_result(iop, probe);
        const auto expected = system.state();
        system.restore(checkpoint);
        check(system.run(7, &second) == RunResult{7, true} &&
              system.run(13, &second) == RunResult{13, true}, probe, "System replay failed");
        check(system.state() == expected && first == second, probe,
              "partitioned pending-load replay changed System state or EE trace");
    }
}
}
int main() {
    try {
        direct_observations(); system_replay();
        std::cout << "IOP load-delay observation tests passed (7 probes, both profiles)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
