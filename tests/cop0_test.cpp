#include "critterlink/cpu.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace critterlink;
void check(bool ok, const char* message) { if (!ok) { throw std::runtime_error(message); } }
constexpr std::uint32_t transfer(unsigned rs, unsigned rt, unsigned rd) {
    return 0x40000000u | (rs << 21) | (rt << 16) | (rd << 11);
}
void test_transfers() {
    Memory memory;
    for (unsigned reg : {8u, 12u, 13u, 14u, 30u}) {
        memory.write(0, 4, transfer(0, 3, reg));
        Cpu cpu;
        auto state = cpu.state();
        state.cop0 = {0x80000100, 0x80000010, 0x80000004, 0x00410c06, 0xabcdef04};
        state.gpr[3] = {123, 0x123456789abcdef0};
        cpu.restore(state);
        const std::uint64_t expected = reg == 8 ? 0xffffffff80000100ull :
            reg == 12 ? 0x00410c06ull : reg == 13 ? 0xffffffff80000010ull :
            reg == 14 ? 0xffffffff80000004ull : 0xffffffffabcdef04ull;
        check(cpu.step(memory).retired && cpu.state().gpr[3] == Register128{expected, state.gpr[3].high},
              "MFC0 value/sign extension/upper lane");
    }
    for (unsigned reg : {12u, 14u, 30u}) {
        memory.write(0, 4, transfer(4, 3, reg));
        Cpu cpu;
        auto state = cpu.state();
        state.gpr[3] = {0xfeedface00410c07ull, 0xdeadbeef};
        cpu.restore(state);
        check(cpu.step(memory).retired, "MTC0 rejected supported register");
        const auto& c = cpu.state().cop0;
        check((reg == 12 ? c.status : reg == 14 ? c.epc : c.error_epc) == 0x00410c07,
              "MTC0 failed low word truncation");
        check(cpu.state().gpr == state.gpr, "MTC0 mutated GPRs");
    }
    const std::array<std::uint32_t, 9> rejected{
        transfer(0, 3, 12) | 1u, transfer(4, 3, 14) | 0x400u,
        transfer(1, 3, 12), transfer(0, 3, 9), transfer(4, 3, 8),
        transfer(4, 3, 13), transfer(4, 3, 9), 0x42000019u, transfer(4, 3, 12)};
    for (auto instruction : rejected) {
        memory.write(0, 4, instruction);
        Cpu cpu;
        auto before = cpu.state();
        before.gpr[3].low = 0x8000; // unsupported Status IM7
        before.cop0.epc = 0x100;
        cpu.restore(before);
        const auto result = cpu.step(memory);
        auto after = cpu.state();
        check(result.stop && result.stop->kind == StopKind::unsupported_instruction && !result.retired,
              "reserved COP0 encoding accepted");
        after.stop.reset();
        check(after == before, "unsupported COP0 instruction partially committed");
    }
}
void test_eret() {
    Memory memory;
    memory.write(0, 4, 0x42000018);
    memory.write(4, 4, 0x24030063); // would modify r3 if incorrectly used as delay slot
    memory.write(0x100, 4, 0x42000018);
    for (std::uint32_t status : {0u, 2u, 4u, 6u}) {
        Cpu cpu;
        auto state = cpu.state();
        state.cop0.status = status;
        state.cop0.epc = 0x200;
        state.cop0.error_epc = 0x100;
        cpu.restore(state);
        check(cpu.step(memory).retired, "ERET failed");
        const auto target = (status & 4u) ? 0x100u : 0x200u;
        check(cpu.state().pc == target && cpu.state().next_pc == target + 4 && !cpu.state().delay_slot &&
              cpu.state().gpr[3].low == 0 && cpu.state().cop0.status == (status & ~((status & 4u) ? 4u : 2u)),
              "ERET target/priority/status/delay slot");
        if (status == 6) {
            check(cpu.step(memory).retired && cpu.state().pc == 0x200 && cpu.state().cop0.status == 0,
                  "second ERET failed EXL return after ERL return");
        }
    }
    Cpu cpu;
    auto state = cpu.state();
    state.delay_slot = true;
    state.branch_pc = 0xfffffffc;
    cpu.restore(state);
    check(cpu.step(memory).stop->kind == StopKind::delay_slot_branch, "ERET delay-slot policy");
}
void test_dispatch() {
    Memory memory;
    for (bool bev : {false, true}) {
        memory.write(0, 4, 12);
        Cpu cpu;
        auto state = cpu.state();
        state.cop0.status = bev ? 0x400000u : 0;
        cpu.restore(state);
        const auto trace = cpu.step(memory);
        check(trace.exception == 8u && !trace.retired && !trace.stop && trace.instruction == 12u &&
              cpu.state().pc == (bev ? 0xbfc00380u : 0x80000180u) && cpu.state().cop0.epc == 0 &&
              cpu.state().cop0.status == (state.cop0.status | 2u), "general exception vector");
        if (bev) { check(cpu.step(memory).stop->kind == StopKind::unsupported_access, "boot ROM silently emulated"); }
    }
    memory.write(0x180, 4, 0x8c030003); // nested unaligned load
    for (bool bd : {false, true}) {
        Cpu cpu(0x80000180);
        auto state = cpu.state();
        state.cop0 = {0x444, bd ? 0x80000024u : 0x24u, 0x1234, 2, 0};
        state.gpr[3].low = 0x5678;
        cpu.restore(state);
        const auto trace = cpu.step(memory);
        check(trace.exception == 4u && cpu.state().cop0.epc == 0x1234 &&
              cpu.state().cop0.cause == (bd ? 0x80000010u : 0x10u) &&
              cpu.state().cop0.bad_vaddr == 3 && cpu.state().gpr == state.gpr,
              "nested EXL exception lost original EPC/BD or failed current cause/address");
    }
    for (bool taken : {false, true}) {
        memory.write(0, 4, 0x10200003); // BEQ r1,zero
        memory.write(4, 4, 13); // BREAK
        Cpu cpu;
        auto state = cpu.state();
        state.gpr[1].low = taken ? 0 : 1;
        cpu.restore(state);
        check(cpu.step(memory).retired, "branch before exception failed");
        const auto trace = cpu.step(memory);
        check(trace.delay_slot && trace.pc == 4 && trace.exception == 9u && !trace.retired &&
              cpu.state().cop0.epc == 0 && cpu.state().cop0.cause == 0x80000024 &&
              !cpu.state().delay_slot && cpu.state().pc == 0x80000180,
              "taken/untaken synchronous delay-slot exception");
    }
    memory.write(0, 4, 12);
    memory.write(0x180, 4, 13);
    Cpu cpu;
    std::vector<InstructionTrace> trace;
    check(cpu.run(memory, 17, &trace) == RunResult{0, true} && trace.size() == 17 &&
          !cpu.state().stop && cpu.state().cop0.epc == 0 && cpu.state().cop0.cause == 36,
          "recursive handler faults escaped instruction-boundary budget");
    const auto saved = cpu.state();
    Cpu restored;
    restored.restore(saved);
    check(cpu.step(memory) == restored.step(memory) && cpu.state() == restored.state(), "handler snapshot replay");
    auto bad_zero = saved;
    bad_zero.gpr[0] = {1, 2};
    restored.restore(bad_zero);
    check(restored.state().gpr[0] == Register128{}, "restore lost hardwired zero");
    const auto before_invalid = restored.state();
    auto invalid = before_invalid;
    invalid.cop0.status |= 0x18u; // reserved privilege mode
    bool rejected = false;
    try { restored.restore(invalid); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && restored.state() == before_invalid, "unsupported Status snapshot was not atomic");
    restored.reset(0x80);
    Cpu fresh(0x80);
    check(restored.state() == fresh.state() && restored.state().cop0 == Cop0State{}, "reset retained COP0/handler state");
}
}
int main() {
    try { test_transfers(); test_eret(); test_dispatch(); std::cout << "COP0 checks passed.\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
