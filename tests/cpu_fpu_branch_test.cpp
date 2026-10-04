#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using namespace critterlink;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr std::uint32_t opcode(unsigned form, std::uint32_t offset) {
    return 0x45000000U | (form << 16U) | offset;
}
CpuState initial(bool condition) {
    CpuState s;
    s.pc = 0x20000; s.next_pc = s.pc + 4;
    s.cop0.status = 0x20000000;
    s.fpu.control = condition ? 0x0183c079U : 0x0103c079U;
    s.fpu.accumulator = 0xdeadbeef;
    for (unsigned i = 0; i < 32; ++i) s.fpu.fpr[i] = 0x80000000U + i;
    return s;
}
void paths() {
    Memory m; Cpu cpu;
    // Literal destinations relative to PC=0x20000, including both signed limits.
    const std::array<std::array<std::uint32_t, 2>, 5> targets{{
        {0, 0x20004}, {3, 0x20010}, {0xffff, 0x20000},
        {0x8000, 4}, {0x7fff, 0x40000}}};
    for (unsigned form = 0; form < 4; ++form) for (bool condition : {false, true})
    for (const auto& target : targets) {
        auto s = initial(condition); auto expected = s;
        const bool taken = condition == ((form & 1U) != 0);
        const bool annul = form >= 2 && !taken;
        m.write(s.pc, 4, opcode(form, target[0]));
        m.write(s.pc + 4, 4, 0x24210001); // addiu r1,r1,1
        cpu.restore(s);
        expected.pc = annul ? 0x20008 : 0x20004;
        expected.next_pc = annul ? 0x2000c : taken ? target[1] : 0x20008;
        expected.delay_slot = !annul; expected.branch_pc = annul ? 0 : s.pc;
        auto trace = cpu.step(m);
        check(trace.retired && !trace.stop && !trace.exception && cpu.state() == expected,
              "COP1 branch destination, annul or preserved state");
        if (!annul) {
            const auto saved = cpu.state(); const auto first = cpu.step(m);
            expected.pc = expected.next_pc; expected.next_pc += 4;
            expected.delay_slot = false; expected.branch_pc = 0; expected.gpr[1].low = 1;
            check(first.retired && first.delay_slot && cpu.state() == expected, "COP1 delay slot");
            cpu.restore(saved);
            check(cpu.step(m) == first && cpu.state() == expected, "COP1 branch replay");
        }
    }
}
void faults() {
    Memory m; Cpu cpu;
    for (unsigned form = 0; form < 4; ++form) {
        auto s = initial(true); s.delay_slot = true; s.branch_pc = s.pc - 4;
        m.write(s.pc, 4, opcode(form, 3)); cpu.restore(s);
        auto t = cpu.step(m); auto expected = s; expected.stop = cpu.state().stop;
        check(t.stop && t.stop->kind == StopKind::delay_slot_branch && !t.retired &&
              cpu.state() == expected, "nested COP1 branch must stop atomically");
        s.cop0.status = 0; cpu.restore(s); t = cpu.step(m);
        check(t.exception == 11U && !t.stop && !t.retired &&
              cpu.state().cop0.cause == 0x9000002cU && cpu.state().cop0.epc == s.branch_pc,
              "disabled COP1 branch exception priority and BD");
        // A faulting delay instruction belongs to the COP1 branch, even if untaken.
        s = initial(false); cpu.restore(s); m.write(s.pc + 4, 4, 0x8c010001);
        check(cpu.step(m).retired, "faulting slot setup");
        if (form == 3) {
            check(cpu.state().pc == s.pc + 8 && cpu.state().cop0.cause == 0,
                  "annulled fault must not dispatch");
        } else {
            t = cpu.step(m);
            check(t.exception == 4U && cpu.state().cop0.epc == s.pc &&
                  cpu.state().cop0.cause == 0x80000010U, "COP1 slot exception EPC/BD");
        }
    }
    for (unsigned form = 4; form < 32; ++form) {
        auto s = initial(false); m.write(s.pc, 4, opcode(form, 0)); cpu.restore(s);
        const auto t = cpu.step(m); s.stop = cpu.state().stop;
        check(t.stop && !t.retired && !t.exception && cpu.state() == s, "reserved branch selector");
    }
}
void sampled_condition() {
    Memory m; Cpu cpu;
    for (unsigned form = 0; form < 4; ++form) {
        const bool condition = (form & 1U) != 0;
        auto s = initial(condition);
        s.gpr[1].low = condition ? 0U : 0x00800000U;
        m.write(s.pc, 4, opcode(form, 3));
        m.write(s.pc + 4, 4, 0x44c1f800); // CTC1 changes C in the slot.
        cpu.restore(s);
        check(cpu.step(m).retired && cpu.step(m).retired && cpu.state().pc == 0x20010 &&
              ((cpu.state().fpu.control & 0x00800000U) != 0) != condition,
              "slot condition write changed an already sampled branch");
    }
    // The offset crosses a virtual address boundary without fetching the target.
    auto s = initial(true); s.pc = 0x80000000; s.next_pc = s.pc + 4;
    m.write(s.pc, 4, opcode(1, 0x8000)); cpu.restore(s);
    check(cpu.step(m).retired && cpu.state().next_pc == 0x7ffe0004,
          "negative branch offset across virtual address boundary");
}
void guest() {
    System system; auto& m = system.memory();
    // CTC1 sets C; BC1T takes its slot, BC1FL annuls a store, BC1F falls through.
    const std::array<std::uint32_t, 10> program{
        0x44c1f800, 0x45010002, 0x24420001, 0x24420040, 0x45020001,
        0xac430004, 0x45000002, 0x24420002, 0xac020400, 0};
    for (std::size_t i = 0; i < program.size(); ++i) m.write(static_cast<std::uint32_t>(i * 4), 4, program[i]);
    CpuState s; s.cop0.status = 0x20000000; s.gpr[1].low = 0x00800000;
    system.cpu().restore(s); check(system.run(2) == RunResult{2, true}, "guest branch setup");
    const auto checkpoint = system.state(); std::vector<InstructionTrace> a, b;
    check(system.run(5, &a) == RunResult{5, true} && m.read(0x400, 4) == 3U &&
          system.cpu().state().pc == 36, "guest branch RAM result");
    const auto expected = system.state(); system.restore(checkpoint);
    system.run(1, &b); system.run(4, &b);
    check(system.state() == expected && a == b, "COP1 full System replay");
}
}
int main() {
    try { paths(); faults(); sampled_condition(); guest(); std::cout << "FPU branch tests passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
