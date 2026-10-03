#include "critterlink/iop.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F function) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "expected rejection");
}
struct Bus final : critterlink::IopBus {
    std::uint32_t address{}, value{};
    std::uint32_t read32(std::uint32_t p) override { address = p; return 0x12345678; }
    void write32(std::uint32_t p, std::uint32_t v) override { address = p; value = v; }
};
struct HalfBus final : critterlink::IopBus {
    std::uint32_t address{};
    std::uint16_t value{};
    std::uint16_t read16(std::uint32_t p) override { address = p; return 0x8765; }
    void write16(std::uint32_t p, std::uint16_t v) override { address = p; value = v; }
    std::uint32_t read32(std::uint32_t) override { throw std::invalid_argument("word forbidden"); }
    void write32(std::uint32_t, std::uint32_t) override { throw std::invalid_argument("word forbidden"); }
};
void tests() {
    using critterlink::Iop;
    Iop iop;
    require(!iop.step(), "IOP must default disabled");
    iop.write32(0x80000100, 0x89abcdef);
    require(iop.read32(0xa0000100) == 0x89abcdef, "RAM aliases");
    require(iop.state().ram[0x100] == 0xef, "little endian");
    iop.write32(Iop::ram_size - 4, 123);
    rejects([&] { iop.write32(Iop::ram_size - 2, 0); });
    rejects([&] { iop.read32(Iop::ram_size); });
    rejects([&] { iop.start(2); });
    rejects([&] { iop.start(0xbfc00000); });
    iop.write8(0xa0000181, 0x87);
    require(iop.read8(0x80000181) == 0x87, "byte RAM aliases");
    iop.write8(Iop::ram_size - 1, 0xaa);
    rejects([&] { iop.read8(Iop::ram_size); });
    iop.write32(0, 0x80010181); // LB
    iop.write32(4, 0);
    iop.write32(8, 0x90020181); // LBU
    iop.write32(12, 0);
    iop.write32(16, 0xa0010183); // SB
    iop.start();
    require(iop.step() && iop.state().gpr[1] == 0, "LB load delay");
    require(iop.step() && iop.state().gpr[1] == 0xffffff87, "LB sign extension");
    require(iop.step() && iop.step() && iop.state().gpr[2] == 0x87, "LBU zero extension");
    require(iop.step() && iop.read8(0x183) == 0x87, "SB low byte");
    iop.write32(0, 0x90010181); iop.write32(4, 0x8c010100); iop.start();
    require(iop.step() && !iop.step(), "byte-word overlapping load rejection");
    Bus byte_bus;
    rejects([&] { byte_bus.read8(0x1f402005); });
    rejects([&] { byte_bus.write8(0x1f402005, 0); });
    iop.write16(0xa0000182, 0x80f1);
    require(iop.read16(0x80000182) == 0x80f1 && iop.state().ram[0x182] == 0xf1,
        "halfword endian and alias");
    iop.write16(Iop::ram_size - 2, 0xffff);
    rejects([&] { iop.write16(0x183, 0); });
    rejects([&] { iop.read16(Iop::ram_size); });
    iop.write32(0, 0x84010182); // LH r1,0x182(zero)
    iop.write32(4, 0x24020007); // Independent load-delay instruction
    iop.write32(8, 0x94030182); // LHU r3,0x182(zero)
    iop.write32(12, 0);
    iop.write32(16, 0xa4010184); // SH r1,0x184(zero)
    iop.start();
    require(iop.step() && iop.state().gpr[1] == 0 && iop.state().pending_load,
        "LH delayed writeback");
    require(iop.step() && iop.state().gpr[1] == 0xffff80f1, "LH sign extension");
    require(iop.step() && iop.step() && iop.state().gpr[3] == 0x80f1, "LHU zero extension");
    require(iop.step() && iop.read16(0x184) == 0x80f1, "SH truncates low halfword");
    iop.write32(0, 0x94010182); iop.write32(4, 0x8c010100); iop.start();
    require(iop.step() && !iop.step(), "mixed-width overlapping loads rejected");
    iop.write32(0, 0x94010183); iop.start();
    require(!iop.step(), "unaligned LHU rejected");
    Bus half_bus;
    rejects([&] { half_bus.read16(0x1f900000); });
    rejects([&] { half_bus.write16(0x1f900000, 0); });
    HalfBus width_bus;
    iop.write32(0, 0x3c01bf90); // KSEG1 alias of SPU register window
    iop.write32(4, 0x94220002); // LHU r2,2(r1)
    iop.write32(8, 0);
    iop.write32(12, 0xa4220004); // SH r2,4(r1)
    iop.start();
    for (unsigned n = 0; n < 4; ++n) require(iop.step(&width_bus), "halfword MMIO instruction");
    require(width_bus.address == 0x1f900004 && width_bus.value == 0x8765,
        "halfword MMIO routes physical address and exact width");
    // Old r1 is read by the load-delay instruction, then new r1 is visible.
    const std::uint32_t program[] = {
        0x24010007, 0x8c010100, 0x24220001, 0x24230001,
        0x2404ffff, 0x24840001, 0xac030104, 0xffffffff
    };
    for (unsigned n = 0; n < 8; ++n) iop.write32(n * 4, program[n]);
    iop.start();
    require(iop.step() && iop.step(), "setup load");
    const auto pending = iop.state();
    require(pending.gpr[1] == 7 && pending.pending_load.has_value(), "load is delayed");
    require(iop.step() && iop.step(), "load consumers");
    require(iop.state().gpr[2] == 8 && iop.state().gpr[3] == 0x89abcdf0, "load delay results");
    const auto after = iop.state();
    iop.restore(pending);
    require(iop.step() && iop.step() && iop.state() == after, "pending load restore");
    require(iop.step() && iop.step() && iop.state().gpr[4] == 0, "32-bit arithmetic wraps");
    require(iop.step() && iop.read32(0x104) == 0x89abcdf0, "store");
    const auto before_fault = iop.state();
    require(!iop.step() && iop.state().stop && iop.state().pc == before_fault.pc, "explicit unsupported stop");
    const auto stopped = iop.state();
    require(!iop.step() && iop.state() == stopped, "stop stable");
    auto malformed = pending;
    malformed.pending_load->reg = 32;
    rejects([&] { iop.restore(malformed); });
    malformed = pending; malformed.gpr[0] = 1;
    rejects([&] { iop.restore(malformed); });
    malformed = pending; malformed.ram.pop_back();
    rejects([&] { iop.restore(malformed); });
    malformed = pending; malformed.next_pc += 4;
    rejects([&] { iop.restore(malformed); });
    require(iop.state() == stopped, "bad restore transactional");
    // Branch delay executes exactly once; branch decision uses old load value.
    iop.write32(0, 0x10000002); // beq zero,zero,+2 ->12
    iop.write32(4, 0x24020005);
    iop.write32(8, 0x24020063);
    iop.write32(12, 0x24420001);
    iop.start();
    require(iop.step() && iop.state().delay_slot && iop.state().pc == 4, "branch scheduled");
    const auto branch = iop.state();
    require(iop.step() && iop.state().pc == 12 && iop.state().gpr[2] == 5, "delay slot executed");
    require(iop.step() && iop.state().gpr[2] == 6, "branch skips instruction");
    const auto branch_after = iop.state();
    iop.restore(branch);
    require(iop.step() && iop.step() && iop.state() == branch_after, "branch snapshot");
    iop.write32(4, 0x08000000); iop.start();
    require(iop.step() && !iop.step(), "branch in delay slot rejected");
    // Peripheral aliases route to physical MMIO, but instruction fetch never does.
    iop.write32(0, 0x3c01bd00); // lui r1,bd00 ->physical1d000000
    iop.write32(4, 0x8c220000);
    iop.write32(8, 0);
    iop.write32(12, 0xac220010);
    iop.start(); Bus bus;
    for (unsigned n = 0; n < 4; ++n) require(iop.step(&bus), "MMIO instruction");
    require(bus.address == 0x1d000010 && bus.value == 0x12345678, "MMIO read/write");
    auto fetch = iop.state(); fetch.pc = 0xbd000000; fetch.next_pc = fetch.pc + 4;
    iop.restore(fetch);
    require(!iop.step(&bus) && bus.address == 0x1d000010, "no MMIO fetch");
    // Same-destination consecutive loads require hardware-specific forwarding.
    iop.write32(0, 0x8c010100); iop.write32(4, 0x8c010104); iop.start();
    require(iop.step(), "first overlapping load");
    const auto overlap = iop.state();
    require(!iop.step() && iop.state().pending_load == overlap.pending_load &&
        iop.state().gpr == overlap.gpr, "unsupported overlapping loads stop atomically");
    // A younger ALU write to a pending-load destination wins.
    iop.write32(0, 0x8c010100); iop.write32(4, 0x24010003); iop.start();
    require(iop.step() && iop.step() && iop.state().gpr[1] == 3, "younger write wins");
    // Logical immediates zero-extend; unsigned register arithmetic wraps.
    const std::uint32_t arithmetic[] = {
        0x3c01ffff, 0x34218001, 0x3022ffff, 0x00211821,
        0x14210002, 0x24040008, 0x08000009, 0x24840001,
        0x24040063, 0x24050030, 0x00a00008, 0x24840001, 0
    };
    for (unsigned n = 0; n < 13; ++n) iop.write32(n * 4, arithmetic[n]);
    iop.start(0x80000000);
    for (unsigned n = 0; n < 4; ++n) require(iop.step(), "arithmetic");
    require(iop.state().gpr[1] == 0xffff8001 && iop.state().gpr[2] == 0x8001 &&
        iop.state().gpr[3] == 0xffff0002, "logical and ADDU results");
    require(iop.step() && iop.state().delay_slot, "not-taken branch has slot");
    require(iop.step() && iop.state().pc == 0x80000018, "not-taken branch continues");
    require(iop.step() && iop.step() && iop.state().pc == 0x80000024 &&
        iop.state().gpr[4] == 9, "J preserves upper PC and executes slot");
    require(iop.step() && iop.step() && iop.step() && iop.state().pc == 0x30 &&
        iop.state().gpr[4] == 10, "JR executes slot");
    iop.write32(0, 0x8c000100); iop.start();
    require(iop.step() && !iop.state().pending_load && iop.state().gpr[0] == 0, "load zero discards");
    iop.write32(0, 0x8c010100);
    // A failed instruction preserves the pending load for the stopped snapshot.
    iop.write32(4, 0x8c020001); iop.start();
    require(iop.step(), "load before fault");
    const auto pre_fault = iop.state();
    require(!iop.step() && iop.state().pending_load == pre_fault.pending_load &&
        iop.state().gpr == pre_fault.gpr, "fault leaves execution unretired");
}
}
int main() {
    try { tests(); std::cout << "IOP tests passed\n"; return EXIT_SUCCESS; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return EXIT_FAILURE; }
}
