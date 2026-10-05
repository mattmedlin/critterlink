#include "critterlink/system.hpp"
#include "iop_muldiv_vectors.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
void check(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
constexpr std::uint32_t operation(unsigned fn, unsigned rs = 1, unsigned rt = 2) {
    return (rs << 21U) | (rt << 16U) | fn;
}
void observations() {
    Iop iop;
    for (bool architectural : {false, true}) for (const auto& vector : iop_muldiv_vectors) {
        iop.start(); iop.write32(0, operation(vector.fn));
        auto before = iop.state();
        before.architectural_exceptions = architectural;
        for (unsigned reg = 1; reg < 32; ++reg) { before.gpr[reg] = 0x76540000 + reg; }
        before.gpr[1] = vector.left; before.gpr[2] = vector.right;
        before.hi = 0x01234567; before.lo = 0x89abcdef;
        before.cop0 = {0x400000, 0x300, 0x12345678, 0x87654321};
        iop.restore(before); auto expected = before;
        expected.pc = 4; expected.next_pc = 8; expected.hi = vector.hi; expected.lo = vector.lo;
        check(iop.step() && iop.state() == expected, "IOP published multiply/divide result or preserved state differs");
        // A load preceding the divide must not bypass into its source operand.
        before.pending_load = IopLoad{1, 0x10203040}; expected.gpr[1] = 0x10203040;
        iop.restore(before);
        check(iop.step() && iop.state() == expected, "older load changed multiply/divide source timing");
    }
}
void delay_slots_and_zero_register() {
    Iop iop;
    for (unsigned fn : {26U, 27U}) for (bool taken : {false, true}) {
        iop.start(); iop.write32(0, taken ? 0x10000003 : 0x14000003);
        iop.write32(4, operation(fn, 1, 0));
        auto before = iop.state(); before.architectural_exceptions = true;
        before.gpr[1] = 0x80000000; before.hi = 7; before.lo = 9; iop.restore(before);
        check(iop.step(), "divide branch setup"); const auto slot = iop.state();
        check(iop.step() && !iop.state().stop && iop.state().hi == 0x80000000 &&
              iop.state().lo == (fn == 26 ? 1U : 0xffffffffU) && iop.state().pc == (taken ? 16U : 8U),
              "zero divisor in taken/untaken delay slot");
        const auto end = iop.state(); iop.restore(slot);
        check(iop.step() && iop.state() == end, "divide branch checkpoint replay");
    }
    for (unsigned fn : {26U, 27U}) {
        iop.start(); iop.write32(0, operation(fn, 0, 0));
        check(iop.step() && iop.state().hi == 0 && iop.state().lo == 0xffffffff && iop.state().gpr[0] == 0,
              "zero source registers divide result");
        for (auto reserved : {1U << 6U, 1U << 11U}) {
            iop.start(); iop.write32(0, operation(fn, 0, 0) | reserved);
            auto before = iop.state(); before.hi = 0x13579bdf; before.lo = 0x2468ace0;
            before.pending_load = IopLoad{3, 0x12345678}; iop.restore(before);
            check(!iop.step() && iop.state().stop, "reserved divide fields accepted");
            before.stop = iop.state().stop;
            check(iop.state() == before, "reserved divide mutated state before rejection");
        }
    }
}
void guest_and_replay() {
    System system; auto& iop = system.memory().iop(); iop.start();
    auto state = iop.state(); state.architectural_exceptions = true; iop.restore(state);
    system.memory().write(0, 4, 0x1000ffff); system.memory().write(4, 4, 0);
    // Original guest uses literal opcodes (no assembler-inserted zero trap), reads
    // HI/LO and stores all measured zero-divisor outcomes to RAM. Read/write HI/LO
    // instructions are separated by more than the documented two-instruction gap.
    std::uint32_t pc = 0, output = 0x1000;
    const auto emit = [&](std::uint32_t instruction) { iop.write32(pc, instruction); pc += 4; };
    for (const auto& vector : iop_muldiv_vectors) {
        if (vector.fn < 26 || vector.right != 0) { continue; }
        emit(0x3c010000 | (vector.left >> 16U)); emit(0x34210000 | (vector.left & 0xffffU));
        emit(operation(vector.fn, 1, 0)); emit(0x00001810); emit(0x00002012); // DIV / MFHI r3 / MFLO r4
        emit(0xac030000 | output); emit(0xac040000 | (output + 4)); output += 8;
    }
    emit(0x1000ffff); emit(0);
    check(system.run(3).budget_exhausted, "IOP arithmetic guest setup");
    const auto saved = system.state(); std::vector<InstructionTrace> first, second;
    check(system.run(120, &first).budget_exhausted, "IOP arithmetic guest execution");
    output = 0x1000;
    for (const auto& vector : iop_muldiv_vectors) {
        if (vector.fn < 26 || vector.right != 0) { continue; }
        check(iop.read32(output) == vector.hi && iop.read32(output + 4) == vector.lo,
              "IOP arithmetic guest RAM differs from published observations");
        output += 8;
    }
    const auto expected = system.state(); system.restore(saved);
    check(system.run(43, &second).budget_exhausted && system.run(77, &second).budget_exhausted &&
          system.state() == expected && first == second, "IOP arithmetic full-System replay differs");
}
}
int main() {
    try { observations(); delay_slots_and_zero_register(); guest_and_replay();
        std::cout << "IOP arithmetic hardware-observation tests passed (108 vectors)\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
