#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using namespace critterlink;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr std::uint32_t op(unsigned kind, unsigned fs, unsigned fd) {
    const unsigned format = kind == 32 ? 20 : 16;
    return 0x44000000U | (format << 21U) | (fs << 11U) | (fd << 6U) | kind;
}
CpuState initial() {
    CpuState s; s.cop0.status = 0x20000000; s.fpu.control = 0x0183c079;
    s.fpu.accumulator = 0xdeadbeef;
    for (unsigned i = 0; i < 32; ++i) s.fpu.fpr[i] = 0xa0000000U + i;
    return s;
}
void result(Memory& m, Cpu& cpu, unsigned kind, std::uint32_t input, std::uint32_t output,
            unsigned fs = 1, unsigned fd = 2) {
    auto s = initial(); s.fpu.fpr[fs] = input; auto expected = s;
    expected.fpu.fpr[fd] = output;
    if (kind == 5 || kind == 7) expected.fpu.control = 0x01830079;
    expected.pc = 4; expected.next_pc = 8;
    m.write(0, 4, op(kind, fs, fd)); cpu.restore(s); const auto t = cpu.step(m);
    check(t.retired && !t.exception && !t.stop && cpu.state() == expected,
          "FPU unary/conversion result or preserved state");
}
void conversions() {
    Memory m; Cpu cpu;
    // Literal hardware-backed conversion results, plus exact integer boundaries.
    const std::array<std::array<std::uint32_t, 2>, 24> to_word{{
        {0,0}, {0x80000000,0}, {1,0}, {0x807fffff,0}, {0x3f7fffff,0}, {0xbf7fffff,0},
        {0x3f800000,1}, {0xbf800000,0xffffffff}, {0x40000000,2}, {0xc0400000,0xfffffffd},
        {0x3fbeb852,1}, {0x3fc00000,1}, {0x3fc147ae,1},
        {0xbfbeb852,0xffffffff}, {0xbfc00000,0xffffffff}, {0xbfc147ae,0xffffffff},
        {0x4effffff,0x7fffff80}, {0xceffffff,0x80000080},
        {0x4f000000,0x7fffffff}, {0xcf000000,0x80000000},
        {0x7f800000,0x7fffffff}, {0xff800000,0x80000000},
        {0x7fffffff,0x7fffffff}, {0xffffffff,0x80000000}}};
    const std::array<std::array<std::uint32_t, 2>, 20> to_float{{
        {0,0}, {1,0x3f800000}, {0xffffffff,0xbf800000}, {5,0x40a00000},
        {0xffff,0x477fff00}, {0x7fffffff,0x4effffff}, {0x80000000,0xcf000000},
        {0x01000001,0x4b800000}, {0xfeffffff,0xcb800000},
        {0x3f800000,0x4e7e0000}, {0x7f800000,0x4eff0000}, {0xff800000,0xcb000000},
        {0x4effffff,0x4e9dffff}, {0xceffffff,0xce440000},
        {0x3fbeb852,0x4e7efae1}, {0x3fc147ae,0x4e7f051e},
        {0xbfbeb852,0xce80828f}, {0xbfc147ae,0xce807d70},
        {0x1337,0x4599b800}, {0xdeadbeef,0xce054904}}};
    for (const auto& v : to_word) result(m, cpu, 36, v[0], v[1]);
    for (const auto& v : to_float) result(m, cpu, 32, v[0], v[1]);
    // Every integer power of two and every float exponent, including saturation.
    for (unsigned power = 0; power < 31; ++power) {
        result(m, cpu, 32, 1U << power, (power + 127U) << 23U);
        result(m, cpu, 32, 0U - (1U << power), 0x80000000U | ((power + 127U) << 23U));
    }
    for (unsigned exponent = 0; exponent < 256; ++exponent) for (bool negative : {false,true}) {
        const auto bits = (exponent << 23U) | (negative ? 0x80000000U : 0U);
        std::uint32_t output = 0;
        if (exponent >= 158) output = negative ? 0x80000000U : 0x7fffffffU;
        else if (exponent >= 127) {
            output = 1U << (exponent - 127U); if (negative) output = 0U - output;
        }
        result(m, cpu, 36, bits, output);
    }
}
void signs_and_registers() {
    Memory m; Cpu cpu;
    for (auto bits : {0U,0x80000000U,1U,0x807fffffU,0xbf800000U,0x7f800001U,0xffffffffU})
    for (unsigned fs = 0; fs < 32; ++fs) for (unsigned fd = 0; fd < 32; ++fd) {
        result(m, cpu, 5, bits, bits & 0x7fffffffU, fs, fd);
        result(m, cpu, 6, bits, bits, fs, fd);
        result(m, cpu, 7, bits, bits ^ 0x80000000U, fs, fd);
    }
    for (unsigned fs = 0; fs < 32; ++fs) for (unsigned fd = 0; fd < 32; ++fd) {
        result(m, cpu, 32, 0xffffffff, 0xbf800000, fs, fd);
        result(m, cpu, 36, 0xbfc00000, 0xffffffff, fs, fd);
    }
}
void control_flow() {
    Memory m; Cpu cpu;
    for (unsigned kind : {5U,6U,7U,32U,36U}) {
        for (unsigned reserved = 1; reserved < 32; ++reserved) {
            auto s = initial(); m.write(0,4,op(kind,1,2)|(reserved<<16U)); cpu.restore(s);
            const auto t = cpu.step(m); s.stop = cpu.state().stop;
            check(t.stop && !t.retired && !t.exception && cpu.state() == s, "reserved unary ft field");
        }
        for (bool slot : {false,true}) {
            auto s = initial(); s.cop0.status = 0; s.pc = slot ? 4U : 0U;
            s.next_pc = 8; s.delay_slot = slot; m.write(s.pc,4,op(kind,1,2)); cpu.restore(s);
            const auto t = cpu.step(m);
            check(t.exception == 11U && !t.retired && cpu.state().fpu == s.fpu &&
                  cpu.state().cop0.cause == (slot ? 0x9000002cU : 0x1000002cU), "conversion CU1 exception");
        }
        for (auto branch : {0x10000003U,0x14000003U,0x54000003U}) {
            auto s = initial(); s.fpu.fpr[1] = 0xffffffff;
            m.write(0,4,branch); m.write(4,4,op(kind,1,2)); cpu.restore(s);
            check(cpu.step(m).retired, "conversion branch setup");
            if (branch == 0x54000003U) check(cpu.state().pc == 8 && cpu.state().fpu == s.fpu, "conversion annul");
            else {
                const auto saved = cpu.state(); const auto first = cpu.step(m); const auto expected = cpu.state();
                cpu.restore(saved); check(first.retired && first.delay_slot && cpu.step(m) == first && cpu.state() == expected,
                                         "conversion delay replay");
            }
        }
    }
}
void guest() {
    System system; auto& m = system.memory();
    const std::array<std::uint32_t, 8> program{
        op(32,0,1), op(7,1,2), op(5,1,3), op(6,2,4), op(36,4,5),
        0xe4010400, 0xe4050404, 0};
    for (std::size_t i = 0; i < program.size(); ++i) m.write(static_cast<std::uint32_t>(i * 4),4,program[i]);
    auto s = initial(); s.fpu.fpr[0] = 0xfffffffd; system.cpu().restore(s);
    check(system.run(2) == RunResult{2,true}, "conversion guest prefix");
    const auto saved = system.state(); std::vector<InstructionTrace> a,b;
    check(system.run(5,&a) == RunResult{5,true} && m.read(0x400,4) == 0xc0400000U && m.read(0x404,4) == 3U,
          "conversion guest RAM results");
    const auto expected = system.state(); system.restore(saved); system.run(2,&b); system.run(3,&b);
    check(system.state() == expected && a == b, "conversion full System replay");
}
}
int main() {
    try { conversions(); signs_and_registers(); control_flow(); guest(); std::cout << "FPU conversion tests passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
