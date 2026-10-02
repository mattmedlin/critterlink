#include "critterlink/interrupt_demo.hpp"
#include "critterlink/hardware_demo.hpp"

#include <algorithm>
#include <stdexcept>

namespace critterlink {
void prepare_interrupt_demo(System& system) {
    prepare_hardware_demo(system);
    constexpr std::array<std::uint32_t, 5> enable{
        0x3c040001, // LUI r4,1: EIE
        0x34840c01, // ORI r4,r4,IM0|IM1|IE
        0x40846000, // MTC0 r4,Status
        0x08000017, // J 0x5c
        0x00000000
    };
    constexpr std::array<std::uint32_t, 20> handler{
        0x40086800, // MFC0 r8,Cause: capture the sources for this entry
        0x31090400, // ANDI r9,r8,IP0
        0x11200007, // BEQ r9,r0,DMA check at index10
        0x00000000,
        0x3c0a1000, // LUI r10,0x1000
        0xad400010, // SW zero,T0_MODE: disable timer
        0x354af000, // ORI r10,r10,0xf000
        0x240b0200, // ADDIU r11,zero,timer0 status bit
        0xad4b0000, // SW r11,INTC_STAT: acknowledge
        0x26940001, // ADDIU r20,r20,1
        0x31090800, // ANDI r9,r8,IP1
        0x11200006, // BEQ r9,r0,ERET at index18
        0x00000000,
        0x3c0a1000,
        0x354ae000, // ORI r10,r10,0xe000
        0x240b0004, // channel2 completion flag, leave mask enabled
        0xad4b0010, // SW r11,D_STAT: acknowledge
        0x26b50001, // ADDIU r21,r21,1
        0x42000018, // ERET
        0x00000000
    };
    for (std::size_t n = 0; n < enable.size(); ++n) {
        system.memory().write(0x50u + static_cast<std::uint32_t>(n * 4), 4, enable[n]);
    }
    for (std::size_t n = 0; n < handler.size(); ++n) {
        system.memory().write(0x200u + static_cast<std::uint32_t>(n * 4), 4, handler[n]);
    }
}

InterruptDemoResult run_interrupt_demo() {
    System system;
    prepare_interrupt_demo(system);
    system.run(28);
    const auto checkpoint = system.state();
    if (checkpoint.cpu.stop || (checkpoint.cpu.cop0.status & 2u) == 0 ||
        checkpoint.cpu.pc < 0x80000200u || checkpoint.cpu.pc >= 0x80000250u) {
        throw std::runtime_error("interrupt fixture did not reach its handler checkpoint");
    }
    std::vector<InstructionTrace> first, second;
    system.run(100, &first);
    const auto expected = system.state();
    system.restore(checkpoint);
    for (unsigned n = 0; n < 100; ++n) { system.run(1, &second); }
    const auto& hardware = system.memory().hardware();
    const auto& cpu = system.cpu().state();
    return {hardware.now(), cpu.gpr[20].low, cpu.gpr[21].low,
            static_cast<std::size_t>(std::count(hardware.graphics().pixels.begin(),
                                             hardware.graphics().pixels.end(), 0x80402010u)),
            !hardware.int0() && !hardware.int1(),
            !cpu.stop && (cpu.cop0.status & 2u) == 0 && (cpu.pc == 0x5c || cpu.pc == 0x60),
            expected == system.state() && first == second};
}
} // namespace critterlink
