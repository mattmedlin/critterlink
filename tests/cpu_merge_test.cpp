#include "critterlink/cpu.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr std::uint32_t instruction(unsigned op, unsigned rs = 1, unsigned rt = 2, unsigned imm = 0) {
    return (op << 26U) | (rs << 21U) | (rt << 16U) | (imm & 0xffffU);
}
constexpr Register128 old{0xa1b2c3d4e5f60718ULL, 0x0123456789abcdefULL};
void prepare(Cpu& cpu, Memory& memory, unsigned op, std::uint32_t address, unsigned rt = 2, unsigned imm = 0) {
    memory.write(0, 4, instruction(op, 1, rt, imm));
    CpuState s; s.gpr[1] = {address, 0}; s.gpr[2] = old; cpu.restore(s);
}
void load_oracles() {
    Memory memory; Cpu cpu;
    memory.write(0x100, 8, 0x8877665544332211ULL);
    const std::array<std::uint64_t, 4> lwl{0x11f60718, 0x22110718, 0x33221118, 0x44332211};
    const std::array<std::uint64_t, 4> lwr{0x44332211, 0xa1b2c3d4e5443322ULL,
        0xa1b2c3d4e5f64433ULL, 0xa1b2c3d4e5f60744ULL};
    const std::array<std::uint64_t, 8> ldl{0x11b2c3d4e5f60718ULL, 0x2211c3d4e5f60718ULL,
        0x332211d4e5f60718ULL, 0x44332211e5f60718ULL, 0x5544332211f60718ULL,
        0x6655443322110718ULL, 0x7766554433221118ULL, 0x8877665544332211ULL};
    const std::array<std::uint64_t, 8> ldr{0x8877665544332211ULL, 0xa188776655443322ULL,
        0xa1b2887766554433ULL, 0xa1b2c38877665544ULL, 0xa1b2c3d488776655ULL,
        0xa1b2c3d4e5887766ULL, 0xa1b2c3d4e5f68877ULL, 0xa1b2c3d4e5f60788ULL};
    for (unsigned op : {34U, 38U, 26U, 27U}) {
        const unsigned width = op < 30 ? 8U : 4U;
        for (unsigned byte = 0; byte < width; ++byte) {
            for (auto alias : {0U, 0x80000000U, 0xa0000000U}) {
                prepare(cpu, memory, op, alias + 0x100U + byte);
                const auto result = op == 34 ? lwl[byte] : op == 38 ? lwr[byte] : op == 26 ? ldl[byte] : ldr[byte];
                check(cpu.step(memory).retired && cpu.state().gpr[2] == Register128{result, old.high},
                      "merge load fixed oracle/upper lane/alias");
            }
        }
    }
    for (unsigned op : {34U, 38U}) {
        prepare(cpu, memory, op, op == 34 ? 0x107U : 0x104U);
        check(cpu.step(memory).retired && cpu.state().gpr[2].low == 0xffffffff88776655ULL,
              "loaded word sign bit must extend");
    }
    prepare(cpu, memory, 38, 0x107, 2, 0xfffe); // negative offset selects 0x105
    check(cpu.step(memory).retired && cpu.state().gpr[2].low == 0xa1b2c3d4e5887766ULL,
          "partial LWR must preserve existing upper32");
    prepare(cpu, memory, 27, 0x101, 1); // destination aliases the base register
    check(cpu.step(memory).retired && cpu.state().gpr[1].low == 0x0088776655443322ULL,
          "merge load base/destination alias");
    prepare(cpu, memory, 26, 0x107, 0);
    check(cpu.step(memory).retired && cpu.state().gpr[0] == Register128{}, "merge load zero register");
}
void store_oracles() {
    Memory memory; Cpu cpu;
    // Literal selected-byte layouts, with untouched bytes and guards set to 0xcc.
    const std::array<std::uint64_t, 4> swl{0xcccccccccccccce5ULL, 0xcccccccccccce5f6ULL,
        0xcccccccccce5f607ULL, 0xcccccccce5f60718ULL};
    const std::array<std::uint64_t, 4> swr{0xcccccccce5f60718ULL, 0xccccccccf60718ccULL,
        0xcccccccc0718ccccULL, 0xcccccccc18ccccccULL};
    const std::array<std::uint64_t, 8> sdl{0xcccccccccccccca1ULL, 0xcccccccccccca1b2ULL,
        0xcccccccccca1b2c3ULL, 0xcccccccca1b2c3d4ULL, 0xcccccca1b2c3d4e5ULL,
        0xcccca1b2c3d4e5f6ULL, 0xcca1b2c3d4e5f607ULL, old.low};
    const std::array<std::uint64_t, 8> sdr{old.low, 0xb2c3d4e5f60718ccULL,
        0xc3d4e5f60718ccccULL, 0xd4e5f60718ccccccULL, 0xe5f60718ccccccccULL,
        0xf60718ccccccccccULL, 0x0718ccccccccccccULL, 0x18ccccccccccccccULL};
    for (unsigned op : {42U, 46U, 44U, 45U}) {
        const unsigned width = op == 42 || op == 46 ? 4U : 8U;
        for (unsigned byte = 0; byte < width; ++byte) {
            memory.write(0xf8, 8, 0xccccccccccccccccULL);
            memory.write(0x100, 8, 0xccccccccccccccccULL);
            memory.write(0x108, 8, 0xccccccccccccccccULL);
            prepare(cpu, memory, op, 0xa0000100U + byte);
            const auto result = op == 42 ? swl[byte] : op == 46 ? swr[byte] : op == 44 ? sdl[byte] : sdr[byte];
            check(cpu.step(memory).retired && memory.read(0x100, 8) == result, "merge store fixed byte oracle");
            check(memory.read(0xf8, 8) == 0xccccccccccccccccULL &&
                  memory.read(0x108, 8) == 0xccccccccccccccccULL && cpu.state().gpr[2] == old,
                  "merge store clobbered guard or source");
        }
    }
}
void pairs_and_replay() {
    Memory memory; Cpu cpu;
    const std::array<std::uint8_t, 8> bytes{0x01, 0x23, 0x45, 0x87, 0x89, 0xab, 0xcd, 0xef};
    for (unsigned width : {4U, 8U}) {
        for (unsigned offset = 0; offset < width; ++offset) {
            for (bool reverse : {false, true}) {
                for (unsigned n = 0; n < width; ++n) memory.write(0x200U + offset + n, 1, bytes[n]);
                const unsigned left = width == 4 ? 34U : 26U, right = width == 4 ? 38U : 27U;
                prepare(cpu, memory, reverse ? right : left, 0x200U + offset, 2, reverse ? 0U : width - 1U);
                memory.write(4, 4, instruction(reverse ? left : right, 1, 2, reverse ? width - 1U : 0U));
                check(cpu.step(memory).retired, "merge pair first load");
                const auto checkpoint = cpu.state();
                const auto first = cpu.step(memory); const auto expected = cpu.state();
                check(first.retired && expected.gpr[2] == Register128{
                    width == 4 ? 0xffffffff87452301ULL : 0xefcdab8987452301ULL, old.high}, "unaligned load pair");
                cpu.restore(checkpoint);
                check(cpu.step(memory) == first && cpu.state() == expected, "mid-load pair snapshot replay");
                const unsigned sl = width == 4 ? 42U : 44U, sr = width == 4 ? 46U : 45U;
                prepare(cpu, memory, reverse ? sr : sl, 0x300U + offset, 2, reverse ? 0U : width - 1U);
                memory.write(4, 4, instruction(reverse ? sl : sr, 1, 2, reverse ? width - 1U : 0U));
                check(cpu.run(memory, 2).retired == 2, "unaligned store pair");
                check(memory.read_partial(0x300U + offset, width) == (width == 4 ? 0xe5f60718ULL : old.low),
                      "unaligned store pair bytes");
            }
        }
    }
    // A second instruction fault must not roll back a first successful partial store.
    prepare(cpu, memory, 45, 0xa1ffffffU);
    memory.write(4, 4, instruction(44, 1, 2, 7));
    check(cpu.step(memory).retired && memory.read(0x1ffffffU, 1) == 0x18, "last byte store");
    check(cpu.step(memory).stop.has_value() && memory.read(0x1ffffffU, 1) == 0x18,
          "merge pair incorrectly atomic across two instructions");
}
void faults() {
    Memory memory; Cpu cpu;
    memory.write(0x1fffff8U, 8, old.low);
    bool rejected = false;
    try { memory.write_partial(0xa1fffffeU, 4, 0); }
    catch (const MemoryFault& f) { rejected = f.reason == MemoryError::unmapped && f.access == Access::store; }
    check(rejected && memory.read(0x1fffff8U, 8) == old.low, "partial bus write must validate whole range");
    for (unsigned count : {0U, 9U}) {
        rejected = false;
        try { memory.read_partial(0x100, count); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "invalid partial width");
    }
    for (unsigned op : {26U, 27U, 34U, 38U, 42U, 44U, 45U, 46U}) {
        for (auto address : {0xc0000003U, 0xa2000003U, 0x9000f003U}) {
            prepare(cpu, memory, op, address, op < 40 ? 0U : 2U);
            const auto before = cpu.state(); const auto hw = memory.hardware().state();
            const auto step = cpu.step(memory);
            check(step.stop && step.stop->kind == StopKind::unsupported_access && !step.exception &&
                  cpu.state().gpr == before.gpr && memory.hardware().state() == hw,
                  "merge fault must not mutate register/device or fabricate exception");
            check(step.stop->diagnostic.find(address == 0xc0000003U ? "0xc0000003" :
                  address == 0xa2000003U ? "0xa2000003" : "0x9000f003") != std::string::npos,
                  "merge fault lost original effective address");
        }
        prepare(cpu, memory, op, 0xa2000003U);
        memory.write(4, 4, instruction(op)); memory.write(0, 4, 0x10000003);
        const auto before = cpu.state().gpr;
        check(cpu.step(memory).retired, "branch before failed merge");
        const auto failed = cpu.step(memory);
        check(failed.delay_slot && failed.stop && !failed.exception &&
              failed.stop->diagnostic.find("delay-slot-of=0x00000000") != std::string::npos &&
              cpu.state().gpr == before && memory.read(0x1fffff8U, 8) == old.low,
              "failed merge lost delay context or changed state");
    }
    // Wrapped effective address; read preceding instruction's byte at address zero.
    prepare(cpu, memory, 38, 0xffffffffU, 2, 1);
    const auto word = memory.read(0, 4);
    check(cpu.step(memory).retired && cpu.state().gpr[2].low == (word | 0xffffffff00000000ULL),
          "merge effective address must wrap32");
}
}
int main() {
    try { load_oracles(); store_oracles(); pairs_and_replay(); faults();
        std::cout << "EE merge access tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
