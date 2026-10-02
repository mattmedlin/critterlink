#include "critterlink/cpu.hpp"
#include "critterlink/demo.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace critterlink;

void check(bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

constexpr std::uint32_t r(unsigned function, unsigned rs = 1, unsigned rt = 2,
                          unsigned rd = 3, unsigned shift = 0) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (shift << 6) | function;
}
constexpr std::uint32_t i(unsigned op, unsigned rs = 1, unsigned rt = 3, unsigned immediate = 0) {
    return (op << 26) | (rs << 21) | (rt << 16) | (immediate & 0xffffu);
}

Cpu prepared(Memory& memory, std::uint32_t instruction, std::uint64_t a = 0, std::uint64_t b = 0) {
    memory.write(0, 4, instruction);
    Cpu cpu;
    auto state = cpu.state();
    state.gpr[1].low = a;
    state.gpr[2].low = b;
    state.gpr[3] = {0x1234, 0xfeedface12345678};
    cpu.restore(state);
    return cpu;
}

void test_demo() {
    Memory first_memory;
    Memory second_memory;
    load_demo(first_memory);
    load_demo(second_memory);
    Cpu first;
    Cpu second;
    std::vector<InstructionTrace> first_trace, second_trace;
    check(first.run(first_memory, 25, &first_trace) == RunResult{25, true}, "demo run budget");
    for (unsigned n = 0; n < 25; ++n) { second.run(second_memory, 1, &second_trace); }
    check(first.state() == second.state(), "partitioned architectural state differs");
    check(first_trace == second_trace, "partitioned trace differs");
    check(std::equal(first_memory.bytes().begin(), first_memory.bytes().end(), second_memory.bytes().begin()),
          "deterministic RAM mismatch");
    const auto& state = first.state();
    check(state.pc == 36 && state.next_pc == 40 && !state.delay_slot && !state.stop, "demo final PC");
    const std::array<std::uint64_t, 6> expected{0, 0, 15, 5, 15, 143};
    for (std::size_t n = 0; n < state.gpr.size(); ++n) {
        check(state.gpr[n] == Register128{n < expected.size() ? expected[n] : 0, 0}, "demo register oracle");
    }
    check(first_memory.read(0x100, 4) == 15, "demo memory oracle");
    check(first_memory.bytes()[0x100] == 15 && first_memory.bytes()[0x101] == 0, "demo byte oracle");
    check(first_trace.size() == 25 && first_trace[5].pc == 20 && first_trace[5].delay_slot,
          "demo trace delay slot");
    const auto saved = first.state();
    check(first.run(first_memory, 0) == RunResult{0, true} && first.state() == saved, "zero budget mutated CPU");
    first.reset();
    first_memory.clear();
    load_demo(first_memory);
    first_trace.clear();
    first.run(first_memory, 25, &first_trace);
    check(first.state() == saved && first_trace == second_trace, "reset/replay differs");
}

void test_alu() {
    struct Case { std::uint32_t instruction; std::uint64_t a, b, expected; };
    const Case cases[]{
        {r(33), 0x7fffffff, 1, 0xffffffff80000000},
        {r(33), 0xffffffffffffffff, 1, 0},
        {r(35), 0, 1, 0xffffffffffffffff},
        {r(32), 12, 8, 20}, {r(34), 12, 8, 4},
        {i(8, 1, 3, 0xffff), 1, 0, 0},
        {i(9, 1, 3, 1), 0x7fffffff, 0, 0xffffffff80000000},
        {i(9, 1, 3, 0xffff), 0, 0, 0xffffffffffffffff},
        {r(36), 0xff000000000000ff, 0x0f0000000000000f, 0x0f0000000000000f},
        {r(37), 0x8000000000000000, 1, 0x8000000000000001},
        {r(38), 0x8000000000000001, 1, 0x8000000000000000},
        {r(39), 0, 0, 0xffffffffffffffff},
        {r(42), 0xffffffffffffffff, 0, 1}, {r(42), 0, 0xffffffffffffffff, 0},
        {r(43), 0xffffffffffffffff, 0, 0}, {r(43), 0, 0xffffffffffffffff, 1},
        {i(10, 1, 3, 0xffff), 0, 0, 0}, {i(11, 1, 3, 0xffff), 0, 0, 1},
        {i(12, 1, 3, 0x8000), 0xffffffffffffffff, 0, 0x8000},
        {i(13, 1, 3, 0x8000), 0, 0, 0x8000},
        {i(14, 1, 3, 0xffff), 0xffffffffffffffff, 0, 0xffffffffffff0000},
        {i(15, 0, 3, 0x8000), 0, 0, 0xffffffff80000000},
        {r(0, 0, 2, 3, 31), 0, 1, 0xffffffff80000000},
        {r(2, 0, 2, 3, 1), 0, 0xffffffff80000000, 0x40000000},
        {r(3, 0, 2, 3, 1), 0, 0xffffffff80000000, 0xffffffffc0000000},
        {r(3, 0, 2, 3, 0), 0, 0xffffffff80000000, 0xffffffff80000000},
        {r(4), 33, 1, 2}, {r(6), 63, 0xffffffff80000000, 1},
        {r(7), 63, 0xffffffff80000000, 0xffffffffffffffff},
    };
    Memory memory;
    for (const auto& c : cases) {
        auto cpu = prepared(memory, c.instruction, c.a, c.b);
        check(cpu.step(memory).retired, "ALU instruction stopped " + std::to_string(c.instruction));
        check(cpu.state().gpr[3].low == c.expected, "ALU wrong result " + std::to_string(c.instruction));
        check(cpu.state().gpr[3].high == 0xfeedface12345678, "scalar write clobbered upper 64 bits");
    }
    for (const auto& c : std::array<Case, 5>{{
             {r(32), 0x7fffffff, 1, 0}, {r(32), 0xffffffff80000000, 0xffffffffffffffff, 0},
             {r(34), 0xffffffff80000000, 1, 0}, {r(34), 0x7fffffff, 0xffffffffffffffff, 0},
             {i(8, 1, 3, 1), 0x7fffffff, 0, 0}}}) {
        auto cpu = prepared(memory, c.instruction, c.a, c.b);
        const auto before = cpu.state();
        check(!cpu.step(memory).retired && cpu.state().stop.has_value(), "overflow did not stop");
        check(cpu.state().gpr == before.gpr && cpu.state().pc == 0, "overflow committed instruction");
        check(cpu.state().cop0.cause == 48 && cpu.state().cop0.epc == 0, "overflow exception fields");
    }
    auto zero = prepared(memory, i(9, 0, 0, 123));
    auto state = zero.state();
    state.gpr[0] = {1, 2};
    zero.restore(state);
    zero.step(memory);
    check(zero.state().gpr[0] == Register128{}, "zero register is writable");
    auto zero_overflow = prepared(memory, r(32, 1, 2, 0), 0x7fffffff, 1);
    check(!zero_overflow.step(memory).retired && zero_overflow.state().stop.has_value(),
          "discarding destination suppressed overflow");
}

void test_branches() {
    Memory memory;
    struct Branch { unsigned op; std::uint64_t a, b; bool taken; };
    const Branch cases[]{{4, 7, 7, true}, {4, 7, 8, false}, {5, 7, 8, true}, {5, 7, 7, false},
                         {6, 0, 0, true}, {6, 0xffffffffffffffff, 0, true}, {6, 1, 0, false},
                         {7, 1, 0, true}, {7, 0, 0, false}, {7, 0xffffffffffffffff, 0, false},
                         {4, 0x100000000, 0, false}};
    for (const auto& c : cases) {
        auto cpu = prepared(memory, i(c.op, 1, c.op >= 6 ? 0 : 2, 3), c.a, c.b);
        memory.write(4, 4, i(9, 0, 3, 17));
        check(cpu.step(memory).retired && cpu.state().pc == 4 && cpu.state().delay_slot,
              "branch failed to enter delay slot");
        check(cpu.step(memory).retired && cpu.state().pc == (c.taken ? 16u : 8u), "branch target");
        check(cpu.state().gpr[3].low == 17 && !cpu.state().delay_slot, "delay slot did not execute");
    }
    auto loop = prepared(memory, i(4, 0, 0, 0xffff));
    memory.write(4, 4, 0);
    check(loop.run(memory, 9) == RunResult{9, true} && loop.state().pc == 4, "bounded backward loop");
    for (unsigned op : {2u, 3u}) {
        auto jump = prepared(memory, (op << 26) | 4u);
        jump.run(memory, 2);
        check(jump.state().pc == 16 && jump.state().gpr[31].low == (op == 3 ? 8u : 0u), "J/JAL");
    }
    for (unsigned fn : {8u, 9u}) {
        auto jump = prepared(memory, r(fn, 1, 0, fn == 9 ? 3 : 0), 32);
        jump.run(memory, 2);
        check(jump.state().pc == 32, "JR/JALR target");
        if (fn == 9) { check(jump.state().gpr[3].low == 8, "JALR link"); }
    }
    Cpu alias(0x80000000);
    memory.write(0, 4, (3u << 26) | 4u);
    alias.run(memory, 2);
    check(alias.state().pc == 0x80000010 && alias.state().gpr[31].low == 0xffffffff80000008,
          "jump region or link sign extension");

    auto nested = prepared(memory, i(4, 0, 0, 3));
    memory.write(4, 4, 2u << 26);
    check(nested.run(memory, 10).retired == 1 && nested.state().stop->kind == StopKind::delay_slot_branch,
          "nested delay slot branch did not stop");

    auto paused = prepared(memory, i(4, 0, 0, 3));
    memory.write(4, 4, i(9, 0, 3, 9));
    paused.step(memory);
    Cpu restored;
    restored.restore(paused.state());
    const auto original_trace = paused.step(memory);
    const auto restored_trace = restored.step(memory);
    check(paused.state() == restored.state() && original_trace == restored_trace && restored.state().pc == 16,
          "restoring a pending branch lost delay-slot state");
}

void test_memory() {
    Memory memory;
    memory.write(0x100, 8, 0x8877665544332211);
    check(memory.read(0x80000100, 8) == 0x8877665544332211 &&
          memory.read(0xa0000100, 8) == 0x8877665544332211, "RAM aliases");
    check(memory.read(0x100, 1) == 0x11 && memory.read(0x107, 1) == 0x88 &&
          memory.read(0x102, 2) == 0x4433, "little endian layout");
    memory.write(0xa0000102, 2, 0xabcd);
    check(memory.read(0x80000100, 4) == 0xabcd2211, "aliased partial store");
    memory.write(0x01fffff8, 8, 0x12345678);
    check(memory.read(0x81fffff8, 8) == 0x12345678, "last RAM doubleword");

    struct FaultCase { std::uint32_t address; unsigned width; MemoryError reason; };
    for (const auto& c : std::array<FaultCase, 5>{{
             {3, 4, MemoryError::alignment}, {0x82000000, 4, MemoryError::unmapped},
             {0x9000f020, 4, MemoryError::device}, {0xc0000000, 4, MemoryError::translation},
             {0xfffffffc, 8, MemoryError::alignment}}}) {
        const auto before = memory.read(0x100, 8);
        bool caught = false;
        try { memory.write(c.address, c.width, 0); }
        catch (const MemoryFault& fault) {
            caught = fault.reason == c.reason && fault.address == c.address && fault.access == Access::store;
        }
        check(caught && memory.read(0x100, 8) == before, "invalid bus write contract");
    }
    bool caught = false;
    try { memory.read(0, 3); } catch (const std::invalid_argument&) { caught = true; }
    check(caught, "invalid host API width");

    struct Load { unsigned op, width; std::uint64_t value, expected; };
    const Load loads[]{{32, 1, 0x80, 0xffffffffffffff80}, {36, 1, 0x80, 0x80},
                       {33, 2, 0x8000, 0xffffffffffff8000}, {37, 2, 0x8000, 0x8000},
                       {35, 4, 0x80000000, 0xffffffff80000000}, {39, 4, 0x80000000, 0x80000000},
                       {55, 8, 0x8877665544332211, 0x8877665544332211}};
    for (const auto& c : loads) {
        memory.write(0x100, c.width, c.value);
        auto cpu = prepared(memory, i(c.op, 1, 3, 0xfff8), 0xa0000108);
        check(cpu.step(memory).retired && cpu.state().gpr[3].low == c.expected, "load extension/address");
        check(cpu.state().gpr[3].high == 0xfeedface12345678, "load overwrote upper lane");
    }
    for (const auto& c : std::array<std::array<unsigned, 2>, 4>{{{40, 1}, {41, 2}, {43, 4}, {63, 8}}}) {
        memory.write(0x100, 8, 0);
        auto cpu = prepared(memory, i(c[0], 1, 2, 0), 0x100, 0x8877665544332211);
        check(cpu.step(memory).retired, "store failed");
        const auto expected = c[1] == 8 ? 0x8877665544332211ull :
                              0x8877665544332211ull & ((1ull << (c[1] * 8)) - 1);
        check(memory.read(0x100, 8) == expected, "store width/truncation");
    }
    auto load_zero = prepared(memory, i(35, 0, 0, 1));
    check(!load_zero.step(memory).retired && load_zero.state().cop0.cause == 16,
          "load to zero suppressed memory fault");
    auto wrap = prepared(memory, i(36, 1, 3, 1), 0xffffffff);
    check(wrap.step(memory).retired && wrap.state().gpr[3].low == memory.read(0, 1), "32-bit address wrap");
}

void test_faults_and_cop0() {
    Memory memory;
    for (const auto instruction : {12u, 13u}) {
        auto cpu = prepared(memory, instruction | (123u << 6));
        const auto result = cpu.run(memory, 10);
        check(result == RunResult{0, false} && cpu.state().cop0.cause == (instruction == 12 ? 32u : 36u),
              "SYSCALL/BREAK exception code");
        const auto saved = cpu.state();
        check(cpu.run(memory, 10) == RunResult{0, false} && cpu.state() == saved, "stop is not sticky");
    }
    for (bool taken : {false, true}) {
        auto cpu = prepared(memory, i(4, 1, 0, 3), taken ? 0 : 1);
        memory.write(4, 4, i(35, 0, 3, 1));
        check(cpu.run(memory, 5).retired == 1, "delay slot fault retirement");
        check(cpu.state().cop0.cause == 0x80000010 && cpu.state().cop0.epc == 0 &&
              cpu.state().cop0.bad_vaddr == 1 && cpu.state().pc == 4, "delay slot exception state");
        check(cpu.state().stop->diagnostic.find("delay-slot-of=0x00000000") != std::string::npos,
              "missing delay-slot diagnostic");
    }
    auto store = prepared(memory, i(43, 0, 2, 0x101), 0, 0xffff);
    memory.write(0x100, 8, 0x1234);
    store.step(memory);
    check(store.state().cop0.cause == 20 && memory.read(0x100, 8) == 0x1234, "store alignment atomicity");
    Cpu fetch(3);
    auto trace = fetch.step(memory);
    check(!trace.instruction && !trace.retired && fetch.state().cop0.cause == 16 &&
          fetch.state().cop0.epc == 3 && fetch.state().cop0.bad_vaddr == 3, "misaligned fetch");
    auto jump = prepared(memory, r(8, 1, 0, 0), 3);
    memory.write(4, 4, i(9, 0, 3, 7));
    check(jump.run(memory, 4).retired == 2 && jump.state().gpr[3].low == 7 &&
          jump.state().cop0.epc == 3 && jump.state().cop0.cause == 16,
          "misaligned jump target faulted before its delay slot");
    Cpu missing(0xbfc00000);
    check(missing.step(memory).stop->kind == StopKind::unsupported_access, "firmware fetch silently accepted");
    for (unsigned op : {35u, 43u}) {
        auto cpu = prepared(memory, i(op, 1, 3), 0x9000f020);
        const auto before = cpu.state();
        check(cpu.step(memory).stop->kind == StopKind::unsupported_access &&
              cpu.state().gpr == before.gpr && cpu.state().cop0 == before.cop0,
              "unimplemented device access mutated architectural state");
    }
    Cpu end_of_ram(0x81fffffc);
    memory.write(0x81fffffc, 4, i(4, 0, 0));
    check(end_of_ram.run(memory, 2).retired == 1 && end_of_ram.state().stop->kind == StopKind::unsupported_access &&
          !end_of_ram.state().stop->instruction &&
          end_of_ram.state().stop->diagnostic.find("delay-slot-of=0x81fffffc") != std::string::npos,
          "delay-slot fetch failure lost branch context");
    for (std::uint32_t instruction : {0x70000000u, 0x44000000u, 0x48000000u, 0x40816000u,
                                      0x40016000u, r(0, 1, 2, 3), r(33, 1, 2, 3, 1),
                                      i(15, 1, 3, 0), i(6, 1, 2, 0)}) {
        auto cpu = prepared(memory, instruction);
        const auto before = cpu.state();
        auto stopped = cpu.step(memory);
        check(!stopped.retired && stopped.stop->kind == StopKind::unsupported_instruction, "unsupported decoder");
        check(cpu.state().gpr == before.gpr && cpu.state().pc == 0, "unsupported instruction mutated CPU");
        check(stopped.stop->diagnostic.find("pc=0x00000000 opcode=0x") != std::string::npos,
              "missing opcode diagnostic");
    }
    for (unsigned reg : {8u, 13u, 14u}) {
        auto cpu = prepared(memory, (16u << 26) | (3u << 16) | (reg << 11));
        auto state = cpu.state();
        state.cop0 = {0x80000100, 0x80000010, 0x80000004};
        cpu.restore(state);
        cpu.step(memory);
        const auto expected = reg == 8 ? 0xffffffff80000100ull : reg == 13 ? 0xffffffff80000010ull : 0xffffffff80000004ull;
        check(cpu.state().gpr[3].low == expected && !cpu.state().stop, "MFC0 stage-one read");
    }
}
} // namespace

int main() {
    try {
        test_demo();
        test_alu();
        test_branches();
        test_memory();
        test_faults_and_cop0();
        std::cout << "CPU and memory checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
