#include "critterlink/interrupt_demo.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* reason) { if (!ok) { throw std::runtime_error(reason); } }
void assert_timer(critterlink::Memory& memory) {
    memory.write(0x10000020, 4, 1);
    memory.write(0x10000010, 4, 0x180);
    memory.write(0x1000f010, 4, 0x200);
    memory.advance(1);
}
}

int main() {
    using namespace critterlink;
    try {
        const auto demo = run_interrupt_demo();
        check(demo.ticks == 128 && demo.timer_services == 1 && demo.dma_services == 1,
              "guest did not service each interrupt exactly once");
        check(demo.colored_pixels == 12 && demo.interrupts_acknowledged && demo.returned,
              "guest IRQ acknowledgement/return or DMA pixels failed");
        check(demo.replay_identical, "handler snapshot replay changed complete state or trace");

        Memory memory;
        assert_timer(memory);
        check(memory.hardware().int0(), "timer source missing");
        Cpu cpu;
        // Every global gate and the per-source mask must permit delivery.
        for (const auto status : {0u, 0x10400u, 0x00401u, 0x10001u, 0x10403u, 0x10405u}) {
            cpu.reset();
            auto state = cpu.state(); state.cop0.status = status; cpu.restore(state);
            const auto step = cpu.step(memory);
            check(step.retired && !step.exception && cpu.state().pc == 4,
                  "masked interrupt was dispatched");
            check((cpu.state().cop0.cause & 0x400u) != 0, "masked pending source absent from Cause");
        }
        cpu.reset();
        auto state = cpu.state(); state.cop0.status = 0x10401; cpu.restore(state);
        const auto entry = cpu.step(memory);
        check(!entry.retired && entry.exception == 0u && !entry.stop &&
              cpu.state().pc == 0x80000200 && cpu.state().cop0.epc == 0 &&
              (cpu.state().cop0.status & 2u) != 0, "interrupt entry state");
        // EXL suppresses reentry while the same hardware source stays pending.
        const auto in_handler = cpu.step(memory);
        check(in_handler.retired && !in_handler.exception && cpu.state().cop0.epc == 0,
              "EXL failed to suppress pending interrupt");

        // A pending interrupt at a delay-slot boundary saves the branch address.
        cpu.reset();
        memory.write(0, 4, 0x10000003); // BEQ zero,zero,0x10
        memory.write(4, 4, 0x24c60001); // ADDIU r6,r6,1
        check(cpu.step(memory).retired && cpu.state().delay_slot, "branch did not arm delay slot");
        state = cpu.state(); state.cop0.status = 0x10401; cpu.restore(state);
        const auto delay_entry = cpu.step(memory);
        check(delay_entry.delay_slot && delay_entry.exception == 0u && !delay_entry.retired &&
              cpu.state().cop0.epc == 0 && (cpu.state().cop0.cause & 0x80000000u) != 0 &&
              cpu.state().gpr[6].low == 0, "delay-slot interrupt did not preserve BD/EPC");
        memory.write(0x1000f000, 4, 0x200); // guest-equivalent source acknowledgement
        memory.write(0x200, 4, 0x42000018); // ERET
        check(cpu.step(memory).retired && cpu.state().pc == 0 && !cpu.state().delay_slot,
              "ERET did not resume saved branch");
        cpu.step(memory); cpu.step(memory);
        check(cpu.state().pc == 0x10 && cpu.state().gpr[6].low == 1,
              "restarted branch did not execute exactly one delay slot");

        // A source raised during EXL is delivered once EXL is cleared by ERET.
        Memory pending;
        assert_timer(pending);
        pending.write(0, 4, 0x42000018);
        cpu.reset(); state = cpu.state();
        state.cop0.status = 0x10403; state.cop0.epc = 0x80; cpu.restore(state);
        check(cpu.step(pending).retired && cpu.state().pc == 0x80, "EXL return failed");
        check(cpu.step(pending).exception == 0u && cpu.state().cop0.epc == 0x80,
              "pending interrupt was lost across ERET");

        // INT1 uses its own mask, including when both hardware sources are high.
        Memory dma;
        dma.write(0x1000e000, 4, 1);
        dma.write(0x1000e010, 4, 0x40000);
        dma.write(0x1000a000, 4, 0x101); // zero-length channel2 transfer
        dma.advance(1);
        check(dma.hardware().int1() && !dma.hardware().int0(), "DMA source missing");
        cpu.reset(); state = cpu.state(); state.cop0.status = 0x10401; cpu.restore(state);
        check(cpu.step(dma).retired && (cpu.state().cop0.cause & 0xc00u) == 0x800,
              "INT0 mask incorrectly enabled INT1");
        state = cpu.state(); state.cop0.status = 0x10801; cpu.restore(state);
        check(cpu.step(dma).exception == 0u && cpu.state().cop0.epc == 4,
              "INT1 mask failed to enable DMA interrupt");
        assert_timer(dma);
        cpu.reset(); state = cpu.state(); state.cop0.status = 0x410c01; cpu.restore(state);
        check(cpu.step(dma).exception == 0u && cpu.state().pc == 0xbfc00400 &&
              (cpu.state().cop0.cause & 0xc00u) == 0xc00,
              "simultaneous interrupt sources or BEV vector incorrect");
        dma.write(0x1000e010, 4, 4);
        dma.write(0x1000f000, 4, 0x200);
        state = cpu.state(); state.pc = 0x100; state.next_pc = 0x104; cpu.restore(state);
        check(cpu.step(dma).retired && (cpu.state().cop0.cause & 0xc00u) == 0,
              "deasserted hardware sources remained pending in Cause");

        // Untaken branches also have delay slots; BD/EPC must not depend on taken state.
        Memory untaken;
        untaken.write(0, 4, 0x14000003); // BNE zero,zero,0x10 (not taken)
        untaken.write(4, 4, 0x24c60001);
        untaken.write(0x200, 4, 0x42000018);
        cpu.reset(); cpu.step(untaken);
        assert_timer(untaken);
        state = cpu.state(); state.cop0.status = 0x10401; cpu.restore(state);
        check(cpu.step(untaken).exception == 0u && cpu.state().cop0.epc == 0 &&
              (cpu.state().cop0.cause & 0x80000000u) != 0, "untaken branch lost delay-slot EPC");
        untaken.write(0x1000f000, 4, 0x200);
        cpu.step(untaken); cpu.step(untaken); cpu.step(untaken);
        check(cpu.state().pc == 8 && cpu.state().gpr[6].low == 1, "untaken branch restart failed");

        // Repeated handler faults consume a finite budget despite retiring nothing.
        System exceptions;
        exceptions.memory().write(0, 4, 0x0000000c); // SYSCALL
        exceptions.memory().write(0x180, 4, 0x0000000c);
        const auto initial = exceptions.state();
        std::vector<InstructionTrace> whole, chunks;
        check(exceptions.run(7, &whole) == RunResult{0, true} &&
              exceptions.memory().hardware().now() == 7, "exception loop did not consume finite budget");
        const auto completed = exceptions.state();
        exceptions.restore(initial);
        for (unsigned n = 0; n < 7; ++n) { exceptions.run(1, &chunks); }
        check(exceptions.state() == completed && whole == chunks &&
              exceptions.cpu().state().cop0.epc == 0, "nested exception replay or original EPC changed");

        // Zero budget must not sample or acknowledge pending sources.
        const auto before = cpu.state();
        check(cpu.run(pending, 0) == RunResult{0, true} && cpu.state() == before,
              "zero budget changed interrupt state");
        std::cout << "interrupt integration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
