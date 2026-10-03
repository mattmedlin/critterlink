#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace critterlink;
constexpr std::uint32_t sync(unsigned type) { return (type << 6U) | 15U; }
constexpr std::uint32_t pref(unsigned hint, unsigned offset) {
    return (51U << 26U) | (1U << 21U) | (hint << 16U) | offset;
}
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

void prefetch_is_nonfaulting() {
    Memory memory;
    Cpu cpu;
    // Unaligned RAM, kernel aliases, unmapped/TLB ranges, ROM, FIFO and MMIO.
    constexpr std::array addresses{0x2001U, 0x80002001U, 0xa0002001U, 0x40000000U,
        0xc0000000U, 0xbfc00000U, 0x10006000U, 0x1000f000U, 0x1f808264U, 0xffffffffU};
    unsigned index = 0;
    for (unsigned hint = 0; hint < 32; ++hint)
        for (unsigned offset : {0U, 0x7fffU, 0x8000U, 0xffffU})
            memory.write(index++ * 4U, 4, pref(hint, offset));
    const auto before = memory.state();
    for (auto address : addresses) {
        CpuState initial;
        initial.gpr[1] = {address, 0x1122334455667788ULL};
        initial.gpr[2] = {0xdeadbeef, 0xfedcba9876543210ULL};
        cpu.restore(initial);
        for (unsigned n = 0; n < index; ++n) {
            auto expected = cpu.state(); expected.pc += 4U; expected.next_pc += 4U;
            const auto trace = cpu.step(memory);
            check(trace.retired && !trace.exception && !trace.stop && !trace.stalled,
                  "PREF faulted, stalled or failed to retire");
            check(cpu.state() == expected, "PREF changed architectural state");
        }
    }
    check(memory.state() == before, "PREF touched memory or device state");
}

void sync_encodings_and_order() {
    Memory memory;
    Cpu cpu;
    for (unsigned type = 0; type < 32; ++type) {
        memory.write(0, 4, 0xac220000); // SW r2,0(r1)
        memory.write(4, 4, sync(type));
        memory.write(8, 4, 0x8c230000); // LW r3,0(r1)
        CpuState initial; initial.gpr[1].low = 0x2000; initial.gpr[2].low = 0x12345678;
        initial.gpr[3].high = 0xabcdef;
        cpu.restore(initial);
        check(cpu.step(memory).retired && memory.read(0x2000, 4) == 0x12345678,
              "store not completed before barrier");
        auto expected = cpu.state(); expected.pc = 8; expected.next_pc = 12;
        const auto barrier = cpu.step(memory);
        check(barrier.retired && !barrier.stalled && cpu.state() == expected,
              "SYNC type rejected or changed CPU state");
        check(cpu.step(memory).retired && cpu.state().gpr[3] == Register128{0x12345678, 0xabcdef},
              "load after SYNC missed preceding store");
        // Every reserved field is checked, for both barrier families.
        for (unsigned bit = 11; bit < 26; ++bit) {
            memory.write(0, 4, sync(type) | (1U << bit)); cpu.restore(initial);
            const auto rejected = cpu.step(memory);
            check(rejected.stop && !rejected.retired && !rejected.exception &&
                  cpu.state().pc == 0 && cpu.state().gpr == initial.gpr,
                  "reserved SYNC encoding accepted or partially executed");
        }
    }
}

void delay_slots() {
    Memory memory;
    Cpu cpu;
    for (unsigned type = 0; type < 32; ++type) {
        for (auto branch : {0x10000003U, 0x14000003U}) { // Taken BEQ, untaken BNE.
            memory.write(0, 4, branch); memory.write(4, 4, sync(type)); cpu.reset();
            check(cpu.step(memory).retired, "branch did not retire");
            auto expected = cpu.state();
            const auto rejected = cpu.step(memory);
            check(rejected.stop && !rejected.retired && !rejected.exception && rejected.delay_slot,
                  "SYNC executed in a delay slot");
            expected.stop = cpu.state().stop;
            check(cpu.state() == expected, "rejected delay-slot SYNC changed branch state");
        }
        memory.write(0, 4, 0x54000003); // BNEL r0,r0: annul the slot.
        cpu.reset();
        check(cpu.run(memory, 2) == RunResult{2, true} && cpu.state().pc == 12,
              "annulled SYNC was executed");
    }
    memory.write(0, 4, 0x10000003); memory.write(4, 4, pref(31, 0xffff));
    CpuState initial; initial.gpr[1].low = 0xc0000000; cpu.restore(initial);
    check(cpu.run(memory, 2) == RunResult{2, true} && cpu.state().pc == 16,
          "PREF cannot execute in an ordinary delay slot");
}

void accepted_fifo_is_not_device_completion() {
    System system;
    auto& memory = system.memory();
    memory.write(0x10003000, 4, 8); // Pause GIF consumption.
    constexpr std::array<std::uint64_t, 2> tag{0x1000000000008000ULL, 14};
    for (unsigned n = 0; n < 16; ++n) memory.write_quadword(0x10006000, tag);
    memory.write(0, 4, 0x7c220000); // SQ r2,0(r1)
    memory.write(4, 4, sync(0)); memory.write(8, 4, sync(16));
    CpuState initial; initial.gpr[1].low = 0x10006000; initial.gpr[2] = {tag[0], tag[1]};
    system.cpu().restore(initial);
    std::vector<InstructionTrace> trace;
    check(system.run(2, &trace) == RunResult{0, true} && trace[0].stalled && trace[1].stalled &&
          system.cpu().state().pc == 0, "SYNC overtook unaccepted store");
    memory.write(0x10003000, 4, 0); memory.advance(1); memory.write(0x10003000, 4, 8);
    check(system.run(1) == RunResult{1, true} && memory.hardware().state().gif_fifo.count == 16,
          "store retry not accepted exactly once");
    // Pending DMA cannot enqueue while full. Neither barrier should wait for it.
    memory.write_quadword(0x2000, tag);
    memory.write(0x1000e000, 4, 1); memory.write(0x1000a010, 4, 0x2000);
    memory.write(0x1000a020, 4, 1); memory.write(0x1000a000, 4, 0x101);
    const auto checkpoint = system.state();
    std::vector<InstructionTrace> first, second;
    check(system.run(2, &first) == RunResult{2, true} && system.cpu().state().pc == 12 &&
          memory.hardware().state().gif_fifo.count == 16 && memory.hardware().state().dma.qwords == 1,
          "SYNC waited for device consumption or DMA completion");
    const auto expected = system.state();
    system.restore(checkpoint); system.run(1, &second); system.run(1, &second);
    check(system.state() == expected && first == second, "barrier snapshot replay mismatch");
}
}
int main() {
    try {
        prefetch_is_nonfaulting();
        sync_encodings_and_order();
        delay_slots();
        accepted_fifo_is_not_device_completion();
        std::cout << "PREF/SYNC tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
