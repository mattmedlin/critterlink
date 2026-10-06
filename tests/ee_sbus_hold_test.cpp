#include "critterlink/system.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
constexpr std::uint32_t stat = 0x1000f000, mask = 0x1000f010;
constexpr std::uint32_t timer(unsigned n) { return 0x10000000 + n * 0x800; }
void check(bool value, const char* reason) { if (!value) { throw std::runtime_error(reason); } }
template<class F> void invalid(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("expected invalid_argument");
}
void register_and_edge_behavior() {
    Hardware hardware;
    check(hardware.state().sbus_interrupt_high, "SBUS must reset to inactive high");
    hardware.write(timer(0) + 0x30, 0x12345678);
    hardware.write(timer(1) + 0x30, 0xffffabcd);
    check(hardware.read(timer(0) + 0x30) == 0x5678 &&
          hardware.read(timer(1) + 0x30) == 0xabcd, "HOLD is writable low16 with zero upper16");
    hardware.write(timer(0), 0x1234); hardware.write(timer(1), 0x9876);
    // One enabled counter and one disabled counter are captured identically.
    hardware.write(timer(0) + 0x10, 0x81); hardware.advance(3, {});
    const auto before = hardware.state(); auto expected = before;
    expected.sbus_interrupt_high = false; expected.sbus_external_high = false;
    expected.timers[0].hold = before.timers[0].count;
    expected.timers[1].hold = before.timers[1].count;
    expected.interrupt_status |= 2;
    hardware.set_sbus_interrupt_line(false);
    check(hardware.state() == expected && !hardware.int0(),
          "falling SBUS must capture both counts and latch masked IRQ without ticks or other mutation");
    check(hardware.read(timer(0) + 0x30) == 0x1234 && hardware.read(timer(1) + 0x30) == 0x9876,
          "SBUS captured incorrect timer counts");
    hardware.set_sbus_interrupt_line(false);
    check(hardware.state() == expected, "repeated asserted SBUS fabricated a new edge");
    hardware.write(mask, 2);
    check(hardware.int0(), "unmask did not expose pending SBUS interrupt");
    hardware.write(stat, 0);
    check(hardware.int0(), "writing zero acknowledged SBUS");
    hardware.write(stat, 2);
    check(!hardware.int0() && hardware.read(stat) == 0, "SBUS W1C acknowledgement failed");
    const auto acknowledged = hardware.state(); hardware.set_sbus_interrupt_line(false);
    check(hardware.state() == acknowledged, "asserted line relatched after W1C without rearming");
    hardware.write(timer(0), 0x3333); hardware.write(timer(1), 0x4444);
    auto rising = hardware.state(); rising.sbus_interrupt_high = true; rising.sbus_external_high = true;
    hardware.set_sbus_interrupt_line(true);
    check(hardware.state() == rising && !hardware.int0(), "rising SBUS captured or interrupted");
    hardware.set_sbus_interrupt_line(true);
    check(hardware.state() == rising, "repeated inactive SBUS changed state");
    hardware.set_sbus_interrupt_line(false);
    check(hardware.int0() && hardware.read(timer(0) + 0x30) == 0x3333 &&
          hardware.read(timer(1) + 0x30) == 0x4444, "rearmed falling edge failed to recapture");
    // Already-latched, masked IRQ does not prevent another physical capture.
    hardware.write(mask, 2); hardware.write(timer(0), 0x5555); hardware.write(timer(1), 0x6666);
    hardware.set_sbus_interrupt_line(true); hardware.set_sbus_interrupt_line(false);
    check(hardware.read(stat) == 2 && !hardware.int0() &&
          hardware.read(timer(0) + 0x30) == 0x5555 && hardware.read(timer(1) + 0x30) == 0x6666,
          "pending masked SBUS suppressed fresh HOLD capture");
    const auto stable = hardware.state();
    for (const unsigned index : {2U, 3U}) {
        invalid([&] { static_cast<void>(hardware.read(timer(index) + 0x30)); });
        invalid([&] { hardware.write(timer(index) + 0x30, 1); });
        auto bad = stable; bad.timers[index].hold = 1;
        invalid([&] { hardware.restore(bad); });
        check(hardware.state() == stable, "invalid HOLD register/snapshot mutated hardware");
    }
    // Other pending sources survive the SBUS acknowledgement.
    hardware.write(timer(2) + 0x20, 1); hardware.write(timer(2) + 0x10, 0x180);
    hardware.advance(1, {}); hardware.write(stat, 2);
    check(hardware.read(stat) == 0x800, "SBUS W1C cleared independent timer interrupt");
}
void iop_source_and_combined_line() {
    Hardware hardware;
    check(hardware.iop_sbus_control() == 0 && hardware.state().sbus_external_high,
          "IOP SBUS deterministic initial state");
    hardware.write(timer(0), 0x1122); hardware.write(timer(1), 0x3344);
    const auto before = hardware.state(); auto expected = before;
    expected.iop_sbus_control = 2; expected.sbus_interrupt_high = false;
    expected.timers[0].hold = 0x1122; expected.timers[1].hold = 0x3344;
    expected.interrupt_status |= 2;
    hardware.write_iop_sbus_control(2);
    check(hardware.state() == expected, "IOP SBUS source failed edge capture or consumed time");
    hardware.write_iop_sbus_control(2);
    check(hardware.state() == expected, "repeated IOP source assertion fabricated edge");
    hardware.write(stat, 2);
    check(hardware.iop_sbus_control() == 2 && !hardware.state().sbus_interrupt_high,
          "EE W1C cleared IOP source");
    hardware.write_iop_sbus_control(2);
    check(hardware.read(stat) == 0, "held IOP source relatched after W1C");
    hardware.write_iop_sbus_control(0);
    check(hardware.state().sbus_interrupt_high && hardware.read(stat) == 0,
          "IOP clear created IRQ or failed to release line");
    hardware.write(timer(0), 0x5566); hardware.write_iop_sbus_control(2);
    hardware.write_iop_sbus_control(0);
    check(hardware.read(stat) == 2 && hardware.read(timer(0) + 0x30) == 0x5566,
          "source clear acknowledged independently latched EE status");
    hardware.set_sbus_interrupt_line(false); hardware.write(stat, 2);
    hardware.write(timer(0), 0x7788); hardware.write_iop_sbus_control(2);
    hardware.write_iop_sbus_control(0);
    check(!hardware.state().sbus_interrupt_high && hardware.read(stat) == 0 &&
          hardware.read(timer(0) + 0x30) == 0x5566,
          "IOP source overrode externally held-low line or recaptured without edge");
    hardware.write_iop_sbus_control(2); hardware.set_sbus_interrupt_line(true);
    check(!hardware.state().sbus_interrupt_high && hardware.read(stat) == 0,
          "external release overrode asserted IOP source");
    hardware.write_iop_sbus_control(0);
    check(hardware.state().sbus_interrupt_high, "combined line did not release when both sources inactive");
    const auto good = hardware.state();
    for (const auto value : {1U, 3U, 4U, 0xffffffffU}) {
        invalid([&] { hardware.write_iop_sbus_control(value); });
        check(hardware.state() == good, "unsupported IOP SBUS control bits mutated hardware");
        auto bad = good; bad.iop_sbus_control = value;
        invalid([&] { hardware.restore(bad); });
        check(hardware.state() == good, "invalid IOP SBUS snapshot restore was not atomic");
    }
    auto inconsistent = good; inconsistent.sbus_interrupt_high = false;
    invalid([&] { hardware.restore(inconsistent); });
    check(hardware.state() == good, "inconsistent combined SBUS level accepted or mutated state");
    // Mailbox writes and a zero-length SIF transfer must not assert this source.
    hardware.write(0x1000f200, 0x12345678);
    hardware.write(0x1000f220, 0x10);
    check(hardware.iop_sbus_control() == 0 && hardware.state().sbus_interrupt_high,
          "SIF mailbox fabricated IOP SBUS source");
    hardware.write(0x1000e000, 1); hardware.write(0x1000c400, 0x101);
    hardware.advance(1, {});
    check((hardware.read(0x1000e010) & 0x40) != 0 && hardware.iop_sbus_control() == 0 &&
          hardware.state().sbus_interrupt_high && hardware.read(stat) == 0,
          "SIF DMA completion fabricated SBUS interrupt");
}

void rejected_iop_bus_accesses() {
    struct AccessCase { std::uint32_t opcode, value; };
    constexpr AccessCase cases[]{
        {0xad090000, 1}, {0xad090000, 3}, {0xad090000, 0xffffffff}, // SW unsupported bits
        {0xa5090000, 0}, {0xa1090000, 0}, // SH/SB must not clear source
        {0x85090000, 0}, {0x81090000, 0}  // LH/LB unsupported widths
    };
    for (const auto access : cases) {
        Hardware hardware;
        hardware.write(timer(0), 0x1234); hardware.write(timer(1), 0x5678);
        hardware.write_iop_sbus_control(2);
        auto& iop = hardware.iop(); iop.write32(0, access.opcode); iop.start();
        auto cpu = iop.state(); cpu.gpr[8] = 0x1f801450; cpu.gpr[9] = access.value;
        iop.restore(cpu); const auto before = hardware.state();
        hardware.advance(1, {});
        check(hardware.stop().has_value() && iop.state().stop.has_value(),
              "unsupported IOP SBUS bus access was accepted");
        cpu.stop = iop.state().stop;
        check(iop.state() == cpu && hardware.state().timers == before.timers &&
              hardware.iop_sbus_control() == 2 && !hardware.state().sbus_interrupt_high &&
              hardware.state().sbus_external_high && hardware.read(stat) == 2,
              "rejected IOP SBUS bus access partially changed CPU/source/HOLD state");
    }
}

void memory_contract() {
    Memory memory;
    memory.write(timer(0) + 0x30, 4, 0xabcddcba);
    check(memory.read(timer(0) + 0x30, 4) == 0xdcba, "Memory HOLD word forwarding");
    for (const unsigned width : {1U, 2U, 8U}) {
        bool read_failed = false, write_failed = false;
        try { static_cast<void>(memory.read(timer(0) + 0x30, width)); }
        catch (const MemoryFault&) { read_failed = true; }
        try { memory.write(timer(0) + 0x30, width, 0); }
        catch (const MemoryFault&) { write_failed = true; }
        check(read_failed && write_failed && memory.read(timer(0) + 0x30, 4) == 0xdcba,
              "unsupported HOLD width changed register");
    }
    memory.write(timer(0), 4, 0x8123); memory.write(timer(1), 4, 0xfedc);
    memory.set_sbus_interrupt_line(false);
    check(memory.read(timer(0) + 0x30, 4) == 0x8123 && memory.read(timer(1) + 0x30, 4) == 0xfedc &&
          memory.hardware().now() == 0, "Memory SBUS forwarding consumed time or missed counts");
}
void system_guest_and_replay() {
    System system; auto& memory = system.memory();
    memory.write(0, 4, 0x1000ffff); memory.write(4, 4, 0);
    // Original literal EE handler reads both captured counters, saves them to
    // RAM, acknowledges SBUS only, counts services, and returns through ERET.
    constexpr std::uint32_t handler[]{
        0x3c081000, // LUI r8, 0x1000
        0x8d090030, // LW r9, HOLD0(r8)
        0x8d0a0830, // LW r10, HOLD1(r8)
        0xac091000, // SW r9, 0x1000(zero)
        0xac0a1004, // SW r10, 0x1004(zero)
        0x340b0002, // ORI r11, zero, SBUS bit
        0x3c0c1001, // LUI r12, 0x1001
        0xad8bf000, // SW r11, -0x1000(r12): INTC_STAT
        0x26100001, // ADDIU r16, r16, 1
        0x42000018  // ERET
    };
    std::uint32_t pc = 0x200;
    for (const auto instruction : handler) { memory.write(pc, 4, instruction); pc += 4; }
    auto cpu = system.cpu().state(); cpu.cop0.status = 0x10401; system.cpu().restore(cpu);
    memory.write(mask, 4, 2); memory.write(timer(0), 4, 0x1234); memory.write(timer(1), 4, 0x5678);
    memory.set_sbus_interrupt_line(false);
    const auto low = system.state();
    std::vector<InstructionTrace> first, replay;
    check(system.run(12, &first).budget_exhausted && system.cpu().state().gpr[16].low == 1 &&
          memory.read(0x1000, 4) == 0x1234 && memory.read(0x1004, 4) == 0x5678 &&
          !memory.hardware().int0() && (system.cpu().state().cop0.status & 2) == 0,
          "EE handler did not read HOLD, acknowledge SBUS, and return");
    memory.set_sbus_interrupt_line(false);
    check(!memory.hardware().int0(), "low-line handler acknowledgement fabricated another IRQ");
    memory.write(timer(0), 4, 0xabcd); memory.write(timer(1), 4, 0xef01);
    memory.set_sbus_interrupt_line(true);
    const auto high = system.state();
    memory.set_sbus_interrupt_line(false);
    check(system.run(12, &first).budget_exhausted && system.cpu().state().gpr[16].low == 2 &&
          memory.read(0x1000, 4) == 0xabcd && memory.read(0x1004, 4) == 0xef01 &&
          !memory.hardware().int0(), "rearmed SBUS did not produce second captured guest result");
    const auto expected = system.state();
    system.restore(low);
    check(system.run(5, &replay).budget_exhausted && system.run(7, &replay).budget_exhausted,
          "low checkpoint replay execution");
    memory.set_sbus_interrupt_line(false);
    memory.write(timer(0), 4, 0xabcd); memory.write(timer(1), 4, 0xef01);
    memory.set_sbus_interrupt_line(true);
    check(system.state() == high, "low-line snapshot lost SBUS level or HOLD state");
    memory.set_sbus_interrupt_line(false);
    check(system.run(12, &replay).budget_exhausted && system.state() == expected && first == replay,
          "full-System SBUS edge replay changed state or EE trace");
    system.restore(high); memory.set_sbus_interrupt_line(false);
    check(system.run(12).budget_exhausted && system.state() == expected,
          "rearmed high-line snapshot did not preserve next falling edge");
    auto bad = expected; bad.memory.hardware.timers[2].hold = 1;
    invalid([&] { system.restore(bad); });
    check(system.state() == expected, "invalid HOLD System restore was not atomic");
}
void iop_guest_pulse_and_replay() {
    System system; auto& memory = system.memory(); auto& iop = memory.iop();
    memory.write(0, 4, 0x1000ffff); memory.write(4, 4, 0);
    constexpr std::uint32_t handler[]{
        0x3c081000, 0x8d090030, 0x8d0a0830, 0xac091000, 0xac0a1004,
        0x340b0002, 0x3c0c1001, 0xad8bf000, 0x26100001, 0x42000018
    };
    std::uint32_t pc = 0x200;
    for (const auto word : handler) { memory.write(pc, 4, word); pc += 4; }
    auto cpu = system.cpu().state(); cpu.cop0.status = 0x10401; system.cpu().restore(cpu);
    memory.write(mask, 4, 2); memory.write(timer(0), 4, 0x2468); memory.write(timer(1), 4, 0xace0);
    // Original IOP guest performs SDK-style control read/OR bit1/write, repeats
    // assertion, observes the source, then clears it after the EE has serviced it.
    // The NOP interval is a diagnostic schedule, not a physical pulse-width claim.
    constexpr std::uint32_t guest[]{
        0x3c081f80, 0x8d091450, 0, 0x35290002, 0xad091450,
        0xad091450, 0x8d0a1450, 0, 0xac0a1000,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0xad001450, 0x8d0b1450, 0, 0xac0b1004,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0xad091450, 0xad091450, // second assertion after the first handler returned
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0xad001450, 0x1000ffff, 0
    };
    pc = 0;
    for (const auto word : guest) { iop.write32(pc, word); pc += 4; }
    iop.start();
    check(system.run(2).budget_exhausted && iop.state().pending_load == IopLoad{9, 0},
          "IOP SBUS control read did not use normal pending-load path");
    const auto pending = system.state(); std::vector<InstructionTrace> first, replay;
    check(system.run(3, &first).budget_exhausted && memory.hardware().iop_sbus_control() == 2 &&
          !memory.hardware().state().sbus_interrupt_high && memory.hardware().state().sbus_external_high,
          "IOP guest failed to assert SBUS independently of external line");
    const auto asserted = system.state();
    check(system.run(56, &first).budget_exhausted && !memory.hardware().stop() &&
          system.cpu().state().gpr[16].low == 2 && memory.read(0x1000, 4) == 0x2468 &&
          memory.read(0x1004, 4) == 0xace0 && iop.read32(0x1000) == 2 && iop.read32(0x1004) == 0 &&
          memory.hardware().iop_sbus_control() == 0 && memory.hardware().state().sbus_interrupt_high &&
          !memory.hardware().int0(), "IOP pulse/EE HOLD handler/source acknowledgement result");
    const auto expected = system.state();
    system.restore(asserted);
    check(system.run(56).budget_exhausted && system.state() == expected,
          "asserted IOP source replay changed captured HOLD or retriggered interrupt");
    system.restore(pending);
    check(system.run(4, &replay).budget_exhausted && system.run(55, &replay).budget_exhausted &&
          system.state() == expected && first == replay,
          "pending control-load replay changed full System state or EE trace");
    auto bad = expected; bad.memory.hardware.sbus_interrupt_high = false;
    invalid([&] { system.restore(bad); });
    check(system.state() == expected, "inconsistent SBUS System snapshot restore was not atomic");
}

}
int main() {
    try {
        register_and_edge_behavior(); iop_source_and_combined_line();
        rejected_iop_bus_accesses(); memory_contract();
        system_guest_and_replay(); iop_guest_pulse_and_replay();
        std::cout << "EE SBUS HOLD functional and replay tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
