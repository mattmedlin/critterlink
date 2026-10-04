#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace critterlink;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr std::uint32_t mfsa(unsigned rd) { return (rd << 11U) | 40U; }
constexpr std::uint32_t mtsa(unsigned rs) { return (rs << 21U) | 41U; }
constexpr std::uint32_t set_sa(bool halfword, unsigned rs, unsigned immediate) {
    return (1U << 26U) | (rs << 21U) | ((halfword ? 25U : 24U) << 16U) | immediate;
}
void counts_and_moves() {
    Memory memory;
    Cpu cpu;
    for (bool halfword : {false, true}) {
        const unsigned range = halfword ? 8U : 16U;
        for (unsigned source = 0; source < range; ++source) {
            for (unsigned immediate = 0; immediate < range; ++immediate) {
                // All unused source/immediate bits are deliberately set.
                memory.write(0, 4, set_sa(halfword, 1, (0xffffU & ~(range - 1U)) | immediate));
                CpuState initial; initial.sa = 0xffffffffffffffffULL;
                initial.gpr[1] = {0xfffffffffffffff0ULL | source | (halfword ? 8U : 0U), 0xabcdef};
                cpu.restore(initial);
                auto expected = initial; expected.pc = 4; expected.next_pc = 8;
                expected.sa = (source ^ immediate) * (halfword ? 16U : 8U);
                check(cpu.step(memory).retired && cpu.state() == expected, "SA XOR/mask/count mismatch");
            }
        }
        // r0 is a real input; these encodings are not branches or link writes.
        memory.write(0, 4, set_sa(halfword, 0, 0xffff)); cpu.reset();
        check(cpu.step(memory).retired && cpu.state().sa == (halfword ? 112U : 120U) &&
              !cpu.state().delay_slot && cpu.state().gpr[31] == Register128{}, "SA r0/immediate mismatch");
    }
    // All four-bit guest tokens round-trip through the internal bit count.
    for (std::uint64_t token=0; token<16; ++token) {
        memory.write(0, 4, mfsa(2)); memory.write(4, 4, 0); memory.write(8, 4, 0);
        memory.write(12, 4, 0); memory.write(16, 4, mtsa(2));
        CpuState initial; initial.sa = token * 8; initial.gpr[2] = {1, 0xabcdef}; cpu.restore(initial);
        check(cpu.step(memory).retired && cpu.state().gpr[2] == Register128{token, 0xabcdef},
              "MFSA byte token or upper lane mismatch");
        auto edited = cpu.state(); edited.sa = 0; cpu.restore(edited);
        check(cpu.run(memory, 4) == RunResult{4, true} && cpu.state().sa == token * 8,
              "MTSA byte token restore mismatch");
        memory.write(0, 4, mfsa(0)); cpu.restore(initial);
        check(cpu.step(memory).retired && cpu.state().gpr[0] == Register128{} && cpu.state().sa == token * 8,
              "MFSA r0 changed state");
    }
    memory.write(0, 4, mtsa(0)); CpuState initial; initial.sa = 120; cpu.restore(initial);
    check(cpu.step(memory).retired && cpu.state().sa == 0, "MTSA r0 did not read zero");
    cpu.reset(); check(cpu.state().sa == 0, "reset retained SA");
}
void hardware_tokens() {
    Memory memory; Cpu cpu;
    std::vector<std::uint64_t> inputs{0,1,8,16,0xffff,0x123456789abcdef0ULL,~0ULL};
    for(unsigned bit=0;bit<64;++bit) inputs.push_back(std::uint64_t{1}<<bit);
    for(auto input:inputs) {
        memory.write(0,4,mtsa(1));memory.write(4,4,0);memory.write(8,4,0);memory.write(12,4,0);memory.write(16,4,mfsa(2));
        CpuState before;before.gpr[1]={input,~0ULL};before.gpr[2]={~0ULL,0x1234abcd};cpu.restore(before);
        auto expected=before;expected.pc=20;expected.next_pc=24;expected.cop0.count=5;expected.sa=(input&15U)*8U;expected.gpr[2].low=input&15U;
        check(cpu.run(memory,5)==RunResult{5,true} && cpu.state()==expected,"hardware MTSA/MFSA masking");
    }
    for(bool halfword:{false,true}) for(unsigned value=0;value<32;++value) {
        memory.write(0,4,set_sa(halfword,1,1));memory.write(4,4,mfsa(2));
        CpuState before;before.gpr[1].low=value;cpu.restore(before);
        const auto token=((value^1U)&(halfword?7U:15U))*(halfword?2U:1U);
        check(cpu.run(memory,2)==RunResult{2,true} && cpu.state().gpr[2].low==token,"generated SA hardware token");
    }
    // Published funnel test: MTSA 1 rotates this repeated source right by one byte.
    memory.write(0,4,mtsa(1));memory.write(4,4,0);memory.write(8,4,0);memory.write(12,4,0);
    memory.write(16,4,(28U<<26U)|(2U<<21U)|(2U<<16U)|(3U<<11U)|(27U<<6U)|40U);
    CpuState before;before.gpr[1].low=1;before.gpr[2]={0x9abcdef012345678ULL,0xdeadbeef11223344ULL};cpu.restore(before);
    check(cpu.run(memory,5)==RunResult{5,true} && cpu.state().gpr[3]==Register128{0x449abcdef0123456ULL,0x78deadbeef112233ULL},"published MTSA/QFSRV byte rotation");
}
void reserved_encodings() {
    Memory memory; Cpu cpu;
    for (unsigned bit = 6; bit < 26; ++bit) {
        for (bool to_sa : {false, true}) {
            if (to_sa ? bit >= 21 : (bit >= 11 && bit < 16)) continue;
            memory.write(0, 4, (to_sa ? mtsa(1) : mfsa(2)) | (1U << bit));
            CpuState initial; initial.sa = 88; initial.gpr[1] = {120, 0xfeed}; cpu.restore(initial);
            const auto trace = cpu.step(memory);
            auto expected = initial; expected.stop = cpu.state().stop;
            check(trace.stop && !trace.retired && !trace.exception && cpu.state() == expected,
                  "reserved SA encoding mutated state or retired");
        }
    }
}
void delay_slots() {
    Memory memory; Cpu cpu;
    for (auto instruction : {mfsa(2), mtsa(1), set_sa(false, 1, 3), set_sa(true, 1, 3)}) {
        for (auto branch : {0x10000003U, 0x14000003U}) {
            memory.write(0, 4, branch); memory.write(4, 4, instruction);
            CpuState initial; initial.sa = 48; initial.gpr[1].low = 120; cpu.restore(initial);
            check(cpu.step(memory).retired, "branch failed");
            const auto slot = cpu.step(memory);
            check(slot.retired && slot.delay_slot && !slot.stop &&
                  cpu.state().pc == (branch == 0x10000003U ? 16U : 8U) && !cpu.state().delay_slot,
                  "SA instruction treated as nested branch");
            const auto expected_sa = instruction == mfsa(2) ? 48U : instruction == mtsa(1) ? 64U :
                instruction == set_sa(false, 1, 3) ? 88U : 48U;
            check(cpu.state().sa == expected_sa, "delay-slot SA result");
            if (instruction == mfsa(2)) check(cpu.state().gpr[2].low == 6, "delay-slot MFSA result");
            memory.write(0, 4, 0x54000003); cpu.restore(initial); // Untaken BNEL annuls.
            check(cpu.step(memory).retired && cpu.state().pc == 8 && cpu.state().sa == initial.sa &&
                  cpu.state().gpr == initial.gpr, "annulled SA instruction executed");
        }
    }
}
void context_replay() {
    System system; auto& memory = system.memory();
    // Save to RAM, alter SA, then reload and restore. NOPs obey manual spacing.
    const std::array program{set_sa(false, 1, 3), mfsa(8), 0U, 0U, 0U,
        set_sa(true, 1, 2), mfsa(9), 0xfc880000U, 0xdc8a0000U, 0U, 0U, mtsa(10), mfsa(11)};
    for (unsigned n = 0; n < program.size(); ++n) memory.write(n * 4U, 4, program[n]);
    CpuState initial; initial.gpr[1].low = 1; initial.gpr[4].low = 0x2000;
    system.cpu().restore(initial);
    check(system.run(9) == RunResult{9, true} && system.cpu().state().sa == 48,
          "SA context setup failed");
    const auto checkpoint = system.state();
    std::vector<InstructionTrace> first, second;
    check(system.run(4, &first) == RunResult{4, true} && system.cpu().state().sa == 16 &&
          system.cpu().state().gpr[11].low == system.cpu().state().gpr[8].low &&
          memory.read(0x2000, 8) == system.cpu().state().gpr[8].low, "SA RAM context round trip");
    const auto expected = system.state();
    system.restore(checkpoint); system.run(1, &second); system.run(3, &second);
    check(system.state() == expected && first == second, "SA snapshot replay mismatch");
}
}
int main() {
    try {
        counts_and_moves(); hardware_tokens(); reserved_encodings(); delay_slots(); context_replay();
        std::cout << "SA instruction tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
