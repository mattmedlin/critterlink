#include "critterlink/system.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
constexpr std::uint32_t instruction(unsigned op, unsigned rs = 1, unsigned rt = 2, unsigned imm = 0) {
    return (op << 26U) | (rs << 21U) | (rt << 16U) | (imm & 0xffffU);
}
constexpr Register128 data{0x8877665544332211ULL, 0xffeeddccbbaa0099ULL};
void prepare(Cpu& cpu, Memory& memory, unsigned op, std::uint32_t address, unsigned rt = 2, unsigned imm = 0) {
    memory.write(0, 4, instruction(op, 1, rt, imm));
    CpuState s; s.gpr[1] = {address, 0xfeed}; s.gpr[2] = data; cpu.restore(s);
}
void transfer() {
    Memory memory; Cpu cpu;
    for (unsigned byte = 0; byte < 16; ++byte) {
        for (auto alias : {0U, 0x80000000U, 0xa0000000U}) {
            memory.write(0x100, 8, data.low); memory.write(0x108, 8, data.high);
            prepare(cpu, memory, 30, alias + 0x100U + byte);
            auto s = cpu.state(); s.gpr[2] = {}; cpu.restore(s);
            check(cpu.step(memory).retired && cpu.state().gpr[2] == data,
                  "LQ must mask low4 and fill both64 lanes");
            memory.write(0x100, 8, 0); memory.write(0x108, 8, 0);
            memory.write(0xf8, 8, 0x123); memory.write(0x110, 8, 0x456);
            prepare(cpu, memory, 31, alias + 0x100U + byte);
            check(cpu.step(memory).retired && memory.read(0x100, 8) == data.low &&
                  memory.read(0x108, 8) == data.high, "SQ complete128 transfer");
            check(memory.read(0xf8, 8) == 0x123 && memory.read(0x110, 8) == 0x456,
                  "SQ wrote outside selected16 bytes");
        }
    }
    const std::array<std::uint8_t, 16> expected{0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                                             0x99,0x00,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    for (unsigned i = 0; i < expected.size(); ++i)
        check(memory.read(0x100U+i, 1) == expected[i], "quadword byte order");
    prepare(cpu, memory, 30, 0x10f, 1);
    check(cpu.step(memory).retired && cpu.state().gpr[1] == data, "LQ destination/base alias");
    prepare(cpu, memory, 31, 0x10f, 1);
    check(cpu.step(memory).retired && memory.read(0x100, 8) == 0x10f && memory.read(0x108, 8) == 0xfeed,
          "SQ base/source uses original register");
    prepare(cpu, memory, 31, 0x101, 0);
    check(cpu.step(memory).retired && memory.read(0x100, 8) == 0 && memory.read(0x108, 8) == 0,
          "SQ zero must store128 zeroes");
    prepare(cpu, memory, 30, 0x100, 0);
    check(cpu.step(memory).retired && cpu.state().gpr[0] == Register128{}, "LQ zero register");
    memory.write_quadword(0x100, {data.low, data.high});
    prepare(cpu, memory, 30, 0x110, 2, 0xffff);
    check(cpu.step(memory).retired && cpu.state().gpr[2] == data, "LQ negative offset before masking");
    prepare(cpu, memory, 31, 0xffffffffU, 2, 1);
    check(cpu.step(memory).retired && memory.read_quadword(0) == std::array{data.low, data.high},
          "SQ effective address wraps before alignment mask");
    prepare(cpu, memory, 31, 0xa1ffffffU);
    check(cpu.step(memory).retired && memory.read(0x1fffff0U, 8) == data.low &&
          memory.read(0x1fffff8U, 8) == data.high, "last RAM quadword");
    prepare(cpu, memory, 30, 0x81ffffffU);
    check(cpu.step(memory).retired && cpu.state().gpr[2] == data, "last RAM quadword load");
}
void faults() {
    Memory memory; Cpu cpu;
    for (unsigned op : {30U, 31U}) {
        for (auto address : {0xc000000fU, 0xa200000fU, 0x9000f00fU, 0xfffffffeU}) {
            prepare(cpu, memory, op, address, op == 30 ? 0U : 2U);
            const auto before = cpu.state(); const auto hw = memory.hardware().state();
            const auto step = cpu.step(memory);
            check(step.stop && step.stop->kind == StopKind::unsupported_access && !step.exception &&
                  cpu.state().gpr == before.gpr && memory.hardware().state() == hw,
                  "unsupported quadword access must not mutate CPU/device");
            if (address == 0xc000000fU)
                check(step.stop->diagnostic.find("address=0xc0000000") != std::string::npos,
                      "LQ/SQ masking must precede translation");
        }
        prepare(cpu, memory, op, 0xa200000fU);
        memory.write(4, 4, instruction(op)); memory.write(0, 4, 0x10000003);
        const auto before = cpu.state().gpr;
        check(cpu.step(memory).retired, "branch before failed quadword access");
        const auto failed = cpu.step(memory);
        check(failed.delay_slot && failed.stop && !failed.exception && cpu.state().gpr == before &&
              failed.stop->diagnostic.find("delay-slot-of=0x00000000") != std::string::npos,
              "failed quadword access lost delay context or changed registers");
    }
    memory.write_quadword(0x1fffff0U, {data.low, data.high});
    bool rejected = false;
    try { memory.write_quadword(0xa1fffff8U, {0,0}); }
    catch (const MemoryFault& f) { rejected = f.reason == MemoryError::alignment; }
    check(rejected && memory.read_quadword(0x1fffff0U) == std::array{data.low, data.high},
          "aligned bus quadword API must reject before any write");
    rejected = false;
    try { memory.read(0, 16); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "scalar bus API must not silently truncate quadword");
}
void replay() {
    System system;
    auto& memory = system.memory();
    memory.write_quadword(0x100, {data.low, data.high});
    memory.write(0, 4, instruction(30, 0, 2, 0x10f));
    memory.write(4, 4, instruction(31, 0, 2, 0x20f));
    memory.write(8, 4, instruction(34, 0, 2, 0x103));
    memory.write(12, 4, instruction(31, 0, 2, 0x21f));
    check(system.run(1).retired == 1, "quadword snapshot setup");
    const auto checkpoint = system.state();
    std::vector<InstructionTrace> first, second;
    check(system.run(3, &first).retired == 3, "quadword replay first run");
    const auto expected = system.state();
    check(memory.read_quadword(0x200) == std::array{data.low, data.high} &&
          memory.read_quadword(0x210) == std::array<std::uint64_t,2>{0x44332211, data.high},
          "quadword/scalar mixed execution oracle");
    system.restore(checkpoint);
    for (unsigned i = 0; i < 3; ++i) check(system.run(1, &second).retired == 1, "quadword replay step");
    check(system.state() == expected && first == second, "quadword full-system restore differs");
}
}
int main() {
    try { transfer(); faults(); replay(); std::cout << "EE quadword tests passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
