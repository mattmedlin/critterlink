#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using namespace critterlink;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr std::uint32_t op(unsigned fn, unsigned fs, unsigned ft, unsigned fd) {
    return 0x46000000U | (ft << 16U) | (fs << 11U) | (fd << 6U) | fn;
}
CpuState initial() {
    CpuState s; s.cop0.status = 0x20000000; s.fpu.control = 0x0183c079;
    s.fpu.accumulator = 0xdeadbeef;
    for (unsigned i = 1; i < 32; ++i) s.gpr[i] = {i, ~std::uint64_t{i}};
    return s;
}
void ordered_vectors() {
    Memory m; Cpu cpu;
    // Ascending raw selection order: distinct zero signs and unflushed fractions.
    constexpr std::array<std::uint32_t, 20> values{
        0xffffffff,0xff800001,0xff800000,0xff7fffff,0xbf800000,
        0x80800000,0x807fffff,0x80000002,0x80000001,0x80000000,
        0,1,2,0x007fffff,0x00800000,0x3f800000,
        0x7f7fffff,0x7f800000,0x7f800001,0x7fffffff};
    for (std::size_t i = 0; i < values.size(); ++i)
    for (std::size_t j = 0; j < values.size(); ++j)
    for (unsigned fn : {40U,41U}) for (unsigned fd : {0U,1U,31U}) {
        auto s = initial(); s.fpu.fpr[0] = values[i]; s.fpu.fpr[31] = values[j];
        auto expected = s; expected.pc = 4; expected.next_pc = 8;
        expected.fpu.control = 0x01830079;
        expected.fpu.fpr[fd] = values[fn == 40 ? (i > j ? i : j) : (i < j ? i : j)];
        m.write(0,4,op(fn,0,31,fd)); cpu.restore(s); const auto t = cpu.step(m);
        check(t.retired && !t.exception && !t.stop && cpu.state() == expected,
              "MIN/MAX ordering, aliases or preserved state");
    }
    // Every encoded source/destination, including all-equal aliases.
    for (unsigned fs = 0; fs < 32; ++fs) for (unsigned ft = 0; ft < 32; ++ft)
    for (unsigned fd = 0; fd < 32; ++fd) for (unsigned fn : {40U,41U}) {
        auto s = initial(); s.fpu.fpr[fs] = 0xbf800000; s.fpu.fpr[ft] = 0x3f800000;
        auto expected = s; expected.pc = 4; expected.next_pc = 8;
        expected.fpu.control = 0x01830079;
        expected.fpu.fpr[fd] = fn == 40 || fs == ft ? 0x3f800000U : 0xbf800000U;
        m.write(0,4,op(fn,fs,ft,fd)); cpu.restore(s);
        check(cpu.step(m).retired && cpu.state() == expected, "MIN/MAX register addressing");
    }
}
void flags_and_flow() {
    Memory m; Cpu cpu;
    for (unsigned fn : {40U,41U}) {
        for (unsigned bit : {3U,4U,5U,6U,14U,15U,16U,17U,23U}) {
            auto s = initial(); s.fpu.control = 0x01000001U | (1U << bit);
            auto expected = s; expected.pc = 4; expected.next_pc = 8;
            expected.fpu.control = bit == 14 || bit == 15 ? 0x01000001U : s.fpu.control;
            m.write(0,4,op(fn,0,0,0)); cpu.restore(s);
            check(cpu.step(m).retired && cpu.state() == expected, "MIN/MAX flag effect");
        }
        for (bool slot : {false,true}) {
            auto s = initial(); s.cop0.status = 0; s.pc = slot ? 4U : 0U;
            s.next_pc = 8; s.delay_slot = slot; m.write(s.pc,4,op(fn,0,31,0)); cpu.restore(s);
            const auto t = cpu.step(m);
            check(t.exception == 11U && !t.retired && !t.stop && cpu.state().fpu == s.fpu &&
                  cpu.state().cop0.cause == (slot ? 0x9000002cU : 0x1000002cU), "MIN/MAX CU1");
        }
        for (auto branch : {0x10000003U,0x14000003U,0x54000003U}) {
            auto s = initial(); s.fpu.fpr[0] = 0x80000000;
            m.write(0,4,branch); m.write(4,4,op(fn,0,31,0)); cpu.restore(s);
            check(cpu.step(m).retired, "MIN/MAX branch setup");
            if (branch == 0x54000003U) check(cpu.state().pc == 8 && cpu.state().fpu == s.fpu, "MIN/MAX annul");
            else {
                const auto saved = cpu.state(); const auto first = cpu.step(m); const auto expected = cpu.state();
                cpu.restore(saved);
                check(first.retired && first.delay_slot && cpu.step(m) == first && cpu.state() == expected,
                      "MIN/MAX slot replay");
            }
        }
    }
    for (unsigned format : {0U,4U,20U,31U}) {
        auto s = initial(); m.write(0,4,0x44000000U|(format<<21U)|40U); cpu.restore(s);
        const auto t = cpu.step(m); s.stop = cpu.state().stop;
        check(t.stop && !t.retired && !t.exception && cpu.state() == s, "unsupported MIN/MAX format");
    }
}
void guest() {
    System system; auto& m = system.memory();
    // Independently observable signed-zero selection and an exponent-zero payload.
    const std::array<std::uint32_t, 7> program{
        op(40,0,1,2), op(41,0,1,3), op(41,4,5,6),
        0xe4020400,0xe4030404,0xe4060408,0};
    for (std::size_t i = 0; i < program.size(); ++i) m.write(static_cast<std::uint32_t>(i*4),4,program[i]);
    auto s = initial(); s.fpu.fpr[0] = 0x80000000; s.fpu.fpr[1] = 0;
    s.fpu.fpr[4] = 1; s.fpu.fpr[5] = 0x3f800000; system.cpu().restore(s);
    check(system.run(2) == RunResult{2,true}, "MIN/MAX guest prefix");
    const auto saved = system.state(); std::vector<InstructionTrace> a,b;
    check(system.run(4,&a) == RunResult{4,true} && m.read(0x400,4) == 0U &&
          m.read(0x404,4) == 0x80000000U && m.read(0x408,4) == 1U, "MIN/MAX guest RAM results");
    const auto expected = system.state(); system.restore(saved); system.run(1,&b); system.run(3,&b);
    check(system.state() == expected && a == b, "MIN/MAX full System replay");
}
}
int main() {
    try { ordered_vectors(); flags_and_flow(); guest(); std::cout << "FPU min/max tests passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
