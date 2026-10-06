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
    expected.sbus_interrupt_high = false;
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
    auto rising = hardware.state(); rising.sbus_interrupt_high = true;
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
}
int main() {
    try {
        register_and_edge_behavior(); memory_contract(); system_guest_and_replay();
        std::cout << "EE SBUS HOLD functional and replay tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
