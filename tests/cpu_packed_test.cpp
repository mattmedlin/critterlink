#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace critterlink;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr std::uint32_t mmi(unsigned fn, unsigned sub, unsigned rs = 1, unsigned rt = 2, unsigned rd = 3) {
    return (28U << 26U) | (rs << 21U) | (rt << 16U) | (rd << 11U) | (sub << 6U) | fn;
}
constexpr Register128 a{0x1716151413121110ULL, 0x1f1e1d1c1b1a1918ULL};
constexpr Register128 b{0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL};
bool bit(Register128 r, unsigned index) {
    return (((index < 64 ? r.low : r.high) >> (index % 64U)) & 1U) != 0;
}
void put(Register128& r, unsigned index, bool value) {
    if (value) (index < 64 ? r.low : r.high) |= std::uint64_t{1} << (index % 64U);
}
CpuState initial_state() {
    CpuState state; state.gpr[1] = a; state.gpr[2] = b;
    state.gpr[3] = {~0ULL, ~0ULL}; state.hi = {42, 43}; state.lo = {44, 45}; state.sa = 24;
    return state;
}
void execute(Memory& memory, Cpu& cpu, std::uint32_t instruction, CpuState initial,
             unsigned rd, Register128 result) {
    memory.write(0, 4, instruction); cpu.restore(initial);
    auto expected = initial; expected.pc = 4; expected.next_pc = 8;
    if (rd != 0) expected.gpr[rd] = result;
    const auto trace = cpu.step(memory);
    check(trace.retired && !trace.exception && !trace.stop && cpu.state() == expected,
          "packed instruction result or preserved state mismatch");
}
void funnel_counts() {
    Memory memory; Cpu cpu;
    for (bool halfword : {false, true}) {
        for (unsigned count = 0; count < (halfword ? 8U : 16U); ++count) {
            for (const auto sources : {std::array{1U, 2U}, std::array{1U, 1U},
                                       std::array{0U, 2U}, std::array{1U, 0U}}) {
                for (unsigned rd : {0U, 1U, 2U, 3U}) {
                    auto initial = initial_state();
                    memory.write(0, 4, (1U << 26U) | ((halfword ? 25U : 24U) << 16U) | count);
                    memory.write(4, 4, mmi(40, 27, sources[0], sources[1], rd));
                    cpu.restore(initial);
                    check(cpu.step(memory).retired, "SA count setup failed");
                    // Byte-array oracle for the concatenation, independent of host word shifts.
                    std::array<unsigned, 32> bytes{};
                    for (unsigned j = 0; j < 32; ++j) {
                        const auto source = initial.gpr[sources[j < 16 ? 1 : 0]];
                        const auto lane = j % 16 < 8 ? source.low : source.high;
                        bytes[j] = static_cast<unsigned>((lane >> ((j % 8) * 8U)) & 255U);
                    }
                    Register128 result;
                    const unsigned offset = count * (halfword ? 2U : 1U);
                    for (unsigned j = 0; j < 16; ++j)
                        (j < 8 ? result.low : result.high) |= std::uint64_t{bytes[offset + j]} << ((j % 8) * 8U);
                    auto expected = cpu.state(); expected.pc = 8; expected.next_pc = 12;
                    if (rd != 0) expected.gpr[rd] = result;
                    check(cpu.step(memory).retired && cpu.state() == expected, "QFSRV byte window/alias mismatch");
                }
            }
        }
    }
    for (auto invalid : {1ULL, 7ULL, 121ULL, 128ULL, 0x100000008ULL, ~0ULL}) {
        for (unsigned rd : {0U, 3U}) {
            auto initial = initial_state(); initial.sa = invalid;
            memory.write(0, 4, mmi(40, 27, 1, 2, rd)); cpu.restore(initial);
            const auto trace = cpu.step(memory); auto expected = initial; expected.stop = cpu.state().stop;
            check(trace.stop && !trace.retired && !trace.exception && cpu.state() == expected,
                  "invalid QFSRV token accepted or partially executed");
        }
    }
}
void packed_logic() {
    Memory memory; Cpu cpu;
    for (unsigned operation = 0; operation < 4; ++operation) {
        const unsigned fn = operation < 2 ? 9U : 41U;
        const unsigned sub = 18U + (operation & 1U);
        for (const auto sources : {std::array{1U, 2U}, std::array{1U, 1U}, std::array{0U, 2U}, std::array{0U, 0U}}) {
            for (unsigned rd : {0U, 1U, 2U, 3U}) {
                auto initial = initial_state();
                // Repeating truth-table inputs, distinct across both 64-bit lanes.
                initial.gpr[1] = {0xccccccccccccccccULL, 0x3333333333333333ULL};
                initial.gpr[2] = {0xaaaaaaaaaaaaaaaaULL, 0x5555555555555555ULL};
                Register128 expected;
                for (unsigned n = 0; n < 128; ++n) {
                    const bool x = bit(initial.gpr[sources[0]], n), y = bit(initial.gpr[sources[1]], n);
                    const bool value = operation == 0 ? (x && y) : operation == 1 ? (x != y) :
                        operation == 2 ? (x || y) : (!x && !y);
                    put(expected, n, value);
                }
                execute(memory, cpu, mmi(fn, sub, sources[0], sources[1], rd), initial, rd, expected);
            }
        }
    }
}
void immediate_shifts() {
    Memory memory; Cpu cpu;
    for (unsigned fn : {52U, 54U, 55U, 60U, 62U, 63U}) {
        const unsigned width = fn < 60 ? 16U : 32U;
        const bool left = fn == 52 || fn == 60, arithmetic = fn == 55 || fn == 63;
        for (unsigned amount = 0; amount < 32; ++amount) {
            for (unsigned rd : {0U, 2U, 3U}) {
                auto initial = initial_state();
                initial.gpr[2] = {0x80017fff0001ffffULL, 0xffff000080000001ULL};
                const auto instruction = mmi(fn, amount, 0, 2, rd);
                if (width == 16 && !left && amount >= 16) {
                    memory.write(0, 4, instruction); cpu.restore(initial);
                    const auto trace = cpu.step(memory); auto expected = initial; expected.stop = cpu.state().stop;
                    check(trace.stop && !trace.retired && cpu.state() == expected, "undefined halfword shift accepted");
                    continue;
                }
                Register128 expected;
                const unsigned count = amount % width;
                // Build each output bit from its source position, with no host signed shifts.
                for (unsigned n = 0; n < 128; ++n) {
                    const unsigned lane_start = (n / width) * width, within = n % width;
                    bool value;
                    if (left) value = within >= count && bit(initial.gpr[2], n - count);
                    else if (within + count < width) value = bit(initial.gpr[2], n + count);
                    else value = arithmetic && bit(initial.gpr[2], lane_start + width - 1U);
                    put(expected, n, value);
                }
                execute(memory, cpu, instruction, initial, rd, expected);
            }
        }
        for (unsigned rs : {1U, 2U, 4U, 8U, 16U}) {
            auto initial = initial_state(); memory.write(0, 4, mmi(fn, 0, rs)); cpu.restore(initial);
            const auto trace = cpu.step(memory); auto expected = initial; expected.stop = cpu.state().stop;
            check(trace.stop && !trace.retired && cpu.state() == expected, "packed shift reserved rs accepted");
        }
        execute(memory, cpu, mmi(fn, 1, 0, 0, 3), initial_state(), 3, {});
    }
    for (auto instruction : {mmi(9, 1), mmi(41, 17), mmi(40, 0)}) {
        memory.write(0, 4, instruction); cpu.reset();
        check(cpu.step(memory).stop.has_value(), "unimplemented neighboring MMI operation accepted");
    }
}
void guest_replay_and_delays() {
    System system; auto& memory = system.memory();
    memory.write_quadword(0x2000, {a.low, a.high}); memory.write_quadword(0x2010, {b.low, b.high});
    const std::array program{
        0x78810000U, 0x78820010U, // LQ r1,0(r4); LQ r2,16(r4).
        0x04180003U, 0x00005028U, // MTSAB r0,3; MFSA r10.
        0U, 0U, 0U, 0x04180007U, // Properly spaced SA overwrite.
        0U, 0U, 0U, 0x01400029U, // MTSA r10 restores 24 bits.
        mmi(40, 27), mmi(9, 19, 3, 1, 3), 0x7c830020U // QFSRV; PXOR; SQ.
    };
    for (unsigned n = 0; n < program.size(); ++n) memory.write(n * 4U, 4, program[n]);
    CpuState initial; initial.gpr[4].low = 0x2000; system.cpu().restore(initial);
    check(system.run(8) == RunResult{8, true} && system.cpu().state().sa == 56, "packed guest setup");
    const auto checkpoint = system.state(); std::vector<InstructionTrace> first, second;
    check(system.run(7, &first) == RunResult{7, true} && system.cpu().state().sa == 24, "packed guest continuation");
    for (unsigned j = 0; j < 16; ++j)
        check(memory.read(0x2020 + j, 1) == ((j + 3U) ^ (j + 16U)), "guest packed result byte mismatch");
    const auto expected = system.state(); system.restore(checkpoint);
    system.run(4, &second); system.run(3, &second);
    check(system.state() == expected && first == second, "packed guest replay mismatch");

    for (auto instruction : {mmi(40, 27), mmi(9, 18), mmi(41, 19), mmi(63, 31, 0)}) {
        for (auto branch : {0x10000003U, 0x14000003U, 0x54000003U}) {
            memory.write(0, 4, branch); memory.write(4, 4, instruction);
            auto state = initial_state(); system.cpu().restore(state);
            check(system.cpu().step(memory).retired, "packed delay branch setup");
            if (branch == 0x54000003U) {
                check(system.cpu().state().pc == 8 && system.cpu().state().gpr == state.gpr, "annulled packed op executed");
            } else {
                const auto slot = system.cpu().step(memory);
                check(slot.retired && slot.delay_slot && system.cpu().state().pc == (branch == 0x10000003U ? 16U : 8U),
                      "packed delay slot failed");
            }
        }
    }
}
}
int main() {
    try {
        funnel_counts(); packed_logic(); immediate_shifts(); guest_replay_and_delays();
        std::cout << "Packed logical/shift tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
