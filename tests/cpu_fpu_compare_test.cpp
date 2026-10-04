#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using namespace critterlink;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr std::uint32_t op(unsigned fn, unsigned fs, unsigned ft) {
    return 0x46000000U | (ft << 16U) | (fs << 11U) | fn;
}
CpuState initial() {
    CpuState s; s.cop0.status = 0x20000000; s.fpu.control = 0x0183c079;
    s.fpu.accumulator = 0xdeadbeef;
    for (unsigned i = 1; i < 32; ++i) s.gpr[i] = {i, ~std::uint64_t{i}};
    return s;
}
void vectors() {
    Memory memory; Cpu cpu;
    // Independent ascending EE values; exponent-zero encodings belong to one group.
    const std::array<std::vector<std::uint32_t>, 15> groups{{
        {0xffffffff}, {0xff800001}, {0xff800000}, {0xff7fffff},
        {0xc0400000}, {0xbf800000}, {0x80800000},
        {0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x807fffff},
        {0x00800000}, {0x3f800000}, {0x40400000}, {0x7f7fffff},
        {0x7f800000}, {0x7f800001}, {0x7fffffff}}};
    for (std::size_t a = 0; a < groups.size(); ++a)
    for (std::size_t b = 0; b < groups.size(); ++b)
    for (auto left : groups[a]) for (auto right : groups[b])
    for (unsigned fn : {48U, 50U, 52U, 54U}) for (bool previous : {false, true}) {
        auto s = initial(); s.fpu.fpr[0] = left; s.fpu.fpr[31] = right;
        if (!previous) s.fpu.control &= ~0x00800000U;
        auto expected = s;
        const bool result = fn == 50 ? a == b : fn == 52 ? a < b : fn == 54 ? a <= b : false;
        expected.fpu.control = result ? 0x0183c079U : 0x0103c079U;
        expected.pc = 4; expected.next_pc = 8;
        memory.write(0, 4, op(fn, 0, 31)); cpu.restore(s); const auto t = cpu.step(memory);
        check(t.retired && !t.stop && !t.exception && cpu.state() == expected,
              "comparison ordering or preserved flags/registers");
    }
    for (unsigned fs = 0; fs < 32; ++fs) for (unsigned ft = 0; ft < 32; ++ft) {
        auto s = initial(); s.fpu.fpr[fs] = 0xbf800000; s.fpu.fpr[ft] = 0x3f800000;
        auto expected = s; expected.pc = 4; expected.next_pc = 8;
        expected.fpu.control = fs == ft ? 0x0103c079U : 0x0183c079U;
        memory.write(0, 4, op(52, fs, ft)); cpu.restore(s);
        check(cpu.step(memory).retired && cpu.state() == expected, "comparison register addressing");
    }
}
void exceptions_and_slots() {
    Memory memory; Cpu cpu;
    for (unsigned fn : {48U, 50U, 52U, 54U}) {
        for (bool slot : {false, true}) {
            auto s = initial(); s.cop0.status = 0; s.delay_slot = slot;
            s.pc = slot ? 4U : 0U; s.next_pc = 8; memory.write(s.pc, 4, op(fn, 0, 31));
            cpu.restore(s); const auto t = cpu.step(memory);
            check(t.exception == 11U && !t.retired && !t.stop && cpu.state().fpu == s.fpu &&
                  cpu.state().cop0.epc == 0 && cpu.state().cop0.cause == (slot ? 0x9000002cU : 0x1000002cU),
                  "comparison CU1 exception");
        }
        for (auto branch : {0x10000003U, 0x14000003U, 0x54000003U}) {
            auto s = initial(); memory.write(0, 4, branch); memory.write(4, 4, op(fn, 0, 31));
            cpu.restore(s); check(cpu.step(memory).retired, "comparison slot setup");
            if (branch == 0x54000003U) {
                check(cpu.state().pc == 8 && cpu.state().fpu == s.fpu, "comparison annul");
            } else {
                const auto saved = cpu.state(); const auto t = cpu.step(memory); const auto expected = cpu.state();
                cpu.restore(saved);
                check(t.retired && t.delay_slot && cpu.step(memory) == t && cpu.state() == expected,
                      "comparison delay replay");
            }
        }
        for (unsigned fd = 1; fd < 32; ++fd) {
            auto s = initial(); memory.write(0, 4, op(fn, 0, 31) | (fd << 6U)); cpu.restore(s);
            const auto t = cpu.step(memory); s.stop = cpu.state().stop;
            check(t.stop && !t.retired && !t.exception && cpu.state() == s, "reserved comparison field");
        }
    }
    for (unsigned fn = 48; fn < 64; ++fn) {
        if (fn == 48 || fn == 50 || fn == 52 || fn == 54) continue;
        auto s = initial(); memory.write(0, 4, op(fn, 0, 31)); cpu.restore(s);
        const auto t = cpu.step(memory); s.stop = cpu.state().stop;
        check(t.stop && !t.retired && cpu.state() == s, "unsupported comparison function");
    }
}
void guest() {
    System system; auto& m = system.memory();
    // Compare -1 < +1, branch on C, read FCR31 and store flags and slot count.
    const std::array<std::uint32_t, 8> program{
        op(52, 0, 31), 0x45010002, 0x24420001, 0x24420040,
        0x4443f800, 0xac030400, 0xac020404, 0};
    for (std::size_t i = 0; i < program.size(); ++i) m.write(static_cast<std::uint32_t>(i * 4), 4, program[i]);
    auto s = initial(); s.gpr[2].low = 0; s.fpu.fpr[0] = 0xbf800000; s.fpu.fpr[31] = 0x3f800000;
    system.cpu().restore(s); check(system.run(1) == RunResult{1, true}, "guest comparison");
    const auto saved = system.state(); std::vector<InstructionTrace> a, b;
    check(system.run(5, &a) == RunResult{5, true} && m.read(0x400, 4) == 0x0183c079U &&
          m.read(0x404, 4) == 1U, "comparison guest RAM results");
    const auto expected = system.state(); system.restore(saved); system.run(2, &b); system.run(3, &b);
    check(system.state() == expected && a == b, "comparison full System replay");
}
}
int main() {
    try { vectors(); exceptions_and_slots(); guest(); std::cout << "FPU comparison tests passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
