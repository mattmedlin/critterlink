#include "critterlink/system.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
void check(bool value, const char* reason) {
    if (!value) { throw std::runtime_error(reason); }
}

// Published observation: ps2autotests IOP branchdelay, "jalr: rs/rd match:
// 00000002". This original literal guest independently observes the taken target
// with r10 as both source and link. Exact link/CPU snapshots below are additional
// architectural/model invariants, not additional physical hardware observations.
constexpr std::array<std::uint32_t, 13> observed_guest{
    0x340a0120, // 100: ORI r10, zero, 0x120
    0x01405009, // 104: JALR r10, r10
    0x00000000, // 108: delay slot
    0x340b0001, // 10c: incorrect fallthrough marker
    0x0800004a, // 110: J 0x128
    0x00000000, 0x00000000, 0x00000000,
    0x340b0002, // 120: taken target marker
    0xac0a1104, // 124: save link
    0xac0b1100, // 128: save result
    0x1000ffff, // 12c: idle
    0x00000000
};
void profile(Iop& iop, bool architectural, std::uint32_t entry) {
    iop.start(entry);
    auto state = iop.state(); state.architectural_exceptions = architectural;
    iop.restore(state);
}
void install_observation(Iop& iop, bool architectural) {
    profile(iop, architectural, 0x100);
    std::uint32_t pc = 0x100;
    for (const auto word : observed_guest) { iop.write32(pc, word); pc += 4; }
    iop.write32(0x1100, 0xdeadbeef); iop.write32(0x1104, 0xdeadbeef);
}
void check_slot(const Iop& iop) {
    check(iop.state().pc == 0x108 && iop.state().next_pc == 0x120 &&
          iop.state().delay_slot && iop.state().branch_pc == 0x104 &&
          iop.state().gpr[10] == 0x10c, "overlapping JALR did not capture old target and PC+8 link");
}
void check_observation(const Iop& iop) {
    check(!iop.state().stop && iop.read32(0x1100) == 2 && iop.state().gpr[11] == 2,
          "JALR rs=rd result differs from published observation 2");
    check(iop.read32(0x1104) == 0x10c, "overlapping JALR guest saved incorrect link");
}
void observation_and_direct_replay() {
    for (const bool architectural : {false, true}) {
        Iop iop; install_observation(iop, architectural);
        check(iop.step() && iop.step(), "JALR observation setup"); check_slot(iop);
        const auto checkpoint = iop.state();
        for (unsigned n = 0; n < 8; ++n) { check(iop.step(), "JALR observation continuation"); }
        check_observation(iop); const auto expected = iop.state();
        iop.restore(checkpoint);
        for (unsigned n = 0; n < 8; ++n) { check(iop.step(), "JALR direct replay"); }
        check(iop.state() == expected, "JALR delay-slot checkpoint replay differs");
    }
}

// Additional inferred architectural and deterministic execution invariants.
// These are deliberately separate from the one published rs=rd result above.
void operand_and_slot_invariants() {
    for (const bool architectural : {false, true}) {
        for (const bool pending : {false, true}) {
            // Overlap, ordinary link overwritten by slot, source overwritten by
            // slot, discarded link, zero target source, and both fields zero.
            struct Variant { std::uint32_t opcode, slot; unsigned source, link; };
            constexpr std::array<Variant, 6> variants{{
                {0x01405009, 0x340a0077, 10, 10},
                {0x01405809, 0x340b0077, 10, 11},
                {0x01405809, 0x340a0077, 10, 11},
                {0x01400009, 0x340a0077, 10, 0},
                {0x00005009, 0x340a0077, 0, 10},
                {0x00000009, 0x00000000, 0, 0}
            }};
            for (const auto& variant : variants) {
                Iop iop; profile(iop, architectural, pending ? 0xfc : 0x100);
                iop.write32(0xfc, 0x8c0a1000); // LW r10, 0x1000(zero)
                iop.write32(0x1000, 0x300);
                iop.write32(0x100, variant.opcode); iop.write32(0x104, variant.slot);
                auto initial = iop.state(); initial.gpr[10] = 0x200;
                initial.gpr[11] = 0xabcdef; iop.restore(initial);
                if (pending) {
                    check(iop.step() && iop.state().pending_load == IopLoad{10, 0x300},
                          "pending JALR operand setup");
                }
                check(iop.step(), "JALR variant did not retire");
                const auto target = variant.source == 0 ? 0U : 0x200U;
                check(iop.state().pc == 0x104 && iop.state().next_pc == target &&
                      iop.state().delay_slot && !iop.state().pending_load && iop.state().gpr[0] == 0,
                      "JALR target capture or delayed-load completion differs");
                if (variant.link != 0) {
                    check(iop.state().gpr[variant.link] == 0x108, "JALR link is not PC+8");
                }
                if (variant.link != 10) {
                    check(iop.state().gpr[10] == (pending ? 0x300U : 0x200U),
                          "older pending load did not complete independently of target capture");
                }
                const auto checkpoint = iop.state();
                check(iop.step() && iop.state().pc == target && !iop.state().delay_slot &&
                      iop.state().gpr[0] == 0, "slot write changed captured JALR destination");
                if (variant.slot != 0) {
                    const unsigned written = variant.slot == 0x340b0077 ? 11 : 10;
                    check(iop.state().gpr[written] == 0x77, "younger slot write lost to link or older load");
                }
                const auto expected = iop.state(); iop.restore(checkpoint);
                check(iop.step() && iop.state() == expected, "JALR variant slot replay differs");
            }
        }
    }
}
void reserved_fields_remain_rejected() {
    for (const bool architectural : {false, true}) {
        // rt and shamt remain reserved even when rs=rd is now supported.
        for (const auto instruction : {0x01415009U, 0x01405049U}) {
            Iop iop; profile(iop, architectural, 0x100); iop.write32(0x100, instruction);
            auto before = iop.state(); before.gpr[10] = 0x200;
            before.pending_load = IopLoad{10, 0x300}; iop.restore(before);
            check(!iop.step() && iop.state().stop.has_value(), "reserved JALR field accepted");
            before.stop = iop.state().stop;
            check(iop.state() == before, "rejected JALR changed state beyond diagnostic stop");
        }
    }
}
void system_replay() {
    for (const bool architectural : {false, true}) {
        System system; auto& iop = system.memory().iop(); install_observation(iop, architectural);
        system.memory().write(0, 4, 0x1000ffff); system.memory().write(4, 4, 0);
        check(system.run(2) == RunResult{2, true}, "JALR full-System setup"); check_slot(iop);
        const auto checkpoint = system.state(); std::vector<InstructionTrace> first, second;
        check(system.run(8, &first) == RunResult{8, true}, "JALR full-System continuation");
        check_observation(iop); const auto expected = system.state(); system.restore(checkpoint);
        check(system.run(1, &second) == RunResult{1, true} &&
              system.run(7, &second) == RunResult{7, true}, "JALR full-System replay");
        check(system.state() == expected && first == second, "JALR System state/trace replay differs");
    }
}
}
int main() {
    try {
        observation_and_direct_replay(); operand_and_slot_invariants();
        reserved_fields_remain_rejected(); system_replay();
        std::cout << "IOP JALR observation and invariant tests passed (both profiles)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
