#include "critterlink/cpu.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr std::uint32_t plzcw(unsigned rs, unsigned rd) {
    return (28U << 26U) | (rs << 21U) | (rd << 11U) | 4U;
}
void results() {
    Memory memory; Cpu cpu;
    const auto run = [&](std::uint64_t input, std::uint64_t expected) {
        for (unsigned rd : {0U, 1U, 2U}) {
            memory.write(0, 4, plzcw(1, rd));
            CpuState initial; initial.sa = 104;
            initial.gpr[1] = {input, 0x123456789abcdef0ULL};
            initial.gpr[2] = {~0ULL, 0xfedcba9876543210ULL};
            initial.hi = {42, 43}; initial.lo = {44, 45}; cpu.restore(initial);
            auto wanted = initial; wanted.pc = 4; wanted.next_pc = 8;
            if (rd != 0) wanted.gpr[rd].low = expected;
            check(cpu.step(memory).retired && cpu.state() == wanted,
                  "PLZCW result, alias, upper lane or unrelated state mismatch");
        }
    };
    run(0, 0x0000001f0000001fULL);
    run(~0ULL, 0x0000001f0000001fULL);
    run(0x800000007fffffffULL, 0);
    run(0x000fff0fff0ff00fULL, 0x0000000b00000007ULL); // Manual example.
    run(0x00000001fffffffeULL, 0x0000001e0000001eULL);
    // Place the first bit unlike the sign at every position. Expected counts
    // come from the constructed position, independently of a leading-zero API.
    for (unsigned position = 0; position < 31; ++position) {
        const auto positive = std::uint32_t{1} << position;
        const auto negative = ~positive;
        const auto count = 30U - position;
        run((std::uint64_t{negative} << 32U) | positive, (std::uint64_t{count} << 32U) | count);
        run((std::uint64_t{positive} << 32U) | 0x80000000U, std::uint64_t{count} << 32U);
    }
    memory.write(0, 4, plzcw(0, 2)); cpu.reset();
    check(cpu.step(memory).retired && cpu.state().gpr[2].low == 0x0000001f0000001fULL,
          "PLZCW zero-register source");
    for (unsigned bit : {6U, 7U, 8U, 9U, 10U, 16U, 17U, 18U, 19U, 20U}) {
        memory.write(0, 4, plzcw(1, 2) | (1U << bit));
        CpuState initial; initial.gpr[1].low = ~0ULL; cpu.restore(initial);
        const auto trace = cpu.step(memory); auto expected = initial; expected.stop = cpu.state().stop;
        check(trace.stop && !trace.retired && !trace.exception && cpu.state() == expected,
              "PLZCW accepted reserved field");
    }
    memory.write(0, 4, 0x10000003); memory.write(4, 4, plzcw(1, 1));
    CpuState initial; initial.gpr[1] = {0x000fff0fff0ff00fULL, 0xfeed}; cpu.restore(initial);
    check(cpu.step(memory).retired, "PLZCW branch setup");
    const auto checkpoint = cpu.state(); const auto first = cpu.step(memory); const auto expected = cpu.state();
    cpu.restore(checkpoint); const auto second = cpu.step(memory);
    check(first == second && first.retired && first.delay_slot && cpu.state() == expected &&
          expected.pc == 16 && expected.gpr[1] == Register128{0x0000000b00000007ULL, 0xfeed},
          "PLZCW delay-slot replay");
}
}
int main() {
    try { results(); std::cout << "PLZCW tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
