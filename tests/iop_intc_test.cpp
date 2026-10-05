#include "critterlink/system.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
constexpr std::uint32_t stat = 0x1f801070, mask = stat + 4, control = stat + 8;
void check(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid INTC operation accepted");
}
void registers() {
    IopIntc intc;
    check(intc.state() == IopIntcState{} && !intc.irq(), "deterministic initial state");
    for (unsigned bit = 0; bit < 26; ++bit) {
        const auto source = 1U << bit;
        intc.restore({}); intc.sample(source);
        check(intc.read(stat) == source && !intc.irq(), "masked edge must latch");
        intc.write(mask, source); check(!intc.irq(), "global gate");
        intc.write(control, 1); check(intc.irq(), "enabled pending edge");
        check(intc.read(control) == 1 && !intc.irq() && intc.read(control) == 0,
              "I_CTRL reads return old value and disable");
        intc.write(control, 1, 1);
        check(intc.read(control, 1) == 1 && !intc.irq(), "byte I_CTRL side effect");
        intc.write(control, 1); intc.write(mask, 0);
        check(!intc.irq() && intc.read(stat) == source, "mask replacement preserves latch");
        intc.write(mask, source); intc.write(stat, ~source);
        check(!intc.irq(), "write zero acknowledges");
        const auto held = intc.state();
        intc.sample(source); check(intc.state() == held, "held source must not relatch");
        intc.sample(0); intc.sample(source); check(intc.irq(), "new rising edge relatches");
        intc.write(stat, 0xffffffff); check(intc.irq(), "write one preserves pending status");
        intc.restore(held); intc.sample(source);
        check(!intc.irq(), "restore must preserve edge history");
    }
    intc.restore({}); intc.sample(0x2000c); intc.write(mask, 0x2000c); intc.write(control, 1);
    intc.write(stat, ~4U); check(intc.read(stat) == 0x20008 && intc.irq(), "independent source acknowledgement");
    intc.write(mask, 4); check(!intc.irq(), "I_MASK replaces instead of toggles");
    const auto before = intc.state();
    for (auto address : {stat, mask, control}) {
        rejects([&] { (void)intc.read(address, 2); });
        rejects([&] { intc.write(address, 0, 2); });
        rejects([&] { (void)intc.read(address + 1); });
    }
    rejects([&] { (void)intc.read(stat, 1); });
    rejects([&] { intc.write(mask, 0, 1); });
    rejects([&] { intc.sample(0x80000000); });
    for (unsigned field = 0; field < 3; ++field) {
        auto bad = before;
        if (field == 0) { bad.status |= 0x80000000; }
        if (field == 1) { bad.mask |= 0x80000000; }
        if (field == 2) { bad.levels |= 0x80000000; }
        rejects([&] { intc.restore(bad); });
    }
    check(intc.state() == before, "rejected operations mutate controller");
    intc.write(mask, 0xffffffff); check(intc.read(mask) == 0x03ffffff, "reserved mask bits deterministic zero");
}
constexpr std::uint32_t imm(unsigned op, unsigned rs, unsigned rt, unsigned value) {
    return (op << 26U) | (rs << 21U) | (rt << 16U) | value;
}
struct Guest {
    Iop& iop;
    std::uint32_t pc;
    void emit(std::uint32_t word) { iop.write32(pc, word); pc += 4; }
    void li(unsigned reg, std::uint32_t value) {
        emit(imm(15, 0, reg, value >> 16U)); emit(imm(13, reg, reg, value & 0xffffU));
    }
    void store(std::uint32_t address, std::uint32_t value, unsigned width = 4) {
        li(16, address); li(17, value); emit(imm(width == 4 ? 43 : 40, 16, 17, 0));
    }
};
void tick(System& system, unsigned count = 1, std::vector<InstructionTrace>* trace = nullptr) {
    if (!system.run(count, trace).budget_exhausted) {
        throw std::runtime_error("IOP interrupt guest stopped: " +
            system.memory().hardware().stop().value_or("EE stop"));
    }
}
void prepare(System& system, unsigned source) {
    // EE runs an original two-instruction idle loop throughout peripheral service.
    system.memory().write(0, 4, 0x1000ffff); system.memory().write(4, 4, 0);
    auto& iop = system.memory().iop(); iop.start(0x300);
    auto cpu = iop.state(); cpu.architectural_exceptions = true; iop.restore(cpu);
    Guest main{iop, 0x300};
    main.store(mask, source); main.store(control, 1);
    main.li(3, 0x401); main.emit(0x40836000); // MTC0 Status (IEc/IM2)
    if (source == 4) {
        system.memory().cdvd().mount(std::vector<std::uint8_t>(2048, 0x5a));
        main.store(0x1f402006, 0x80, 1);
        main.store(0x1f8010f0, 0x8000);
        main.store(0x1f8010b0, 0x9000);
        main.store(0x1f8010b4, 0x100020);
        main.store(0x1f8010b8, 0x41000200);
        for (auto byte : {0U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U, 1U, 0U}) {
            main.store(0x1f402005, byte, 1);
        }
        main.store(0x1f402004, 6, 1);
    } else if (source == 0x20000) {
        main.store(0x1f808268, 0x3bc);
        main.store(0x1f808200, (5U << 18U) | (5U << 8U) | 0x40U);
        for (auto byte : {1U, 0x42U, 0U, 0U, 0U}) { main.store(0x1f808260, byte, 1); }
        main.store(0x1f808268, 0x3bd);
    } else {
        for (unsigned n = 0; n < 32; ++n) { iop.write32(0x9000 + 4 * n, 0x12340000 + n); }
        system.memory().write(0x1000e000, 4, 1);
        system.memory().write(0x1000c010, 4, 0xa000);
        system.memory().write(0x1000c020, 4, 8);
        system.memory().write(0x1000c000, 4, 0x100);
        main.store(0x1f801570, 0x800);
        main.store(0x1f801578, 1);
        main.store(0x1f8010f4, 0x800000);
        main.store(0x1f801574, 0x40000);
        main.store(0x1f801520, 0x9000);
        main.store(0x1f801524, 0x10020);
        main.store(0x1f801528, 0x01000201);
    }
    main.emit(0x1000ffff); main.emit(0); // BEQ idle loop and delay slot
    Guest handler{iop, 0x80};
    handler.emit(0x401a7000); // MFC0 k0,EPC, delayed
    handler.emit(0x401b6800); // MFC0 k1,Cause, delayed
    handler.emit(0);
    handler.emit(imm(43, 0, 27, 0x1100));
    handler.emit(imm(35, 0, 20, 0x1000)); handler.emit(0);
    handler.emit(imm(9, 20, 20, 1)); handler.emit(imm(43, 0, 20, 0x1000));
    handler.store(stat, ~source);
    if (source == 4) { handler.store(0x1f402008, 2, 1); }
    else if (source == 8) { handler.store(0x1f801574, 0x04040000); }
    else { handler.store(0x1f808280, 1); }
    handler.emit(0x03400008); handler.emit(0x42000010); // JR k0 / RFE
}
void gating_and_control_reads() {
    for (unsigned gates = 0; gates < 16; ++gates) {
        System system; auto& iop = system.memory().iop(); iop.start();
        system.memory().cdvd().write8(0x1f402004, 6); // guest-visible parameter error IRQ
        auto saved = system.state();
        saved.memory.hardware.iop.architectural_exceptions = true;
        saved.memory.hardware.iop.cop0.status = ((gates & 4) ? 0x400U : 0U) | ((gates & 8) ? 1U : 0U);
        saved.memory.hardware.iop_intc.mask = (gates & 2) ? 4U : 0U;
        saved.memory.hardware.iop_intc.enabled = (gates & 1) != 0;
        system.restore(saved); tick(system);
        check(iop.state().pc == (gates == 15 ? 0x80000080U : 4U), "four independent interrupt gates");
        check((iop.state().cop0.cause & 0x400) == ((gates & 3) == 3 ? 0x400U : 0U), "IP2 independent of CPU IE/IM");
        check(system.memory().hardware().state().iop_intc.status == 4, "gates discarded source edge");
    }
    for (unsigned reg : {0U, 2U}) for (bool byte : {false, true}) {
        System system; auto& iop = system.memory().iop(); iop.start();
        iop.write32(0, imm(byte ? 36 : 35, 1, reg, 0));
        iop.write32(4, 0); iop.write32(8, 0);
        auto saved = system.state();
        saved.memory.hardware.iop.gpr[1] = 0xbf801078;
        saved.memory.hardware.iop_intc.enabled = true;
        system.restore(saved); tick(system);
        const auto after_read = system.state();
        check(!after_read.memory.hardware.iop_intc.enabled, "guest I_CTRL read failed to disable");
        check(iop.state().gpr[reg] == 0, "I_CTRL load bypassed load delay");
        tick(system, 2); const auto expected = system.state();
        check(iop.state().gpr[reg] == (reg == 0 ? 0U : 1U), "I_CTRL old value / zero-register semantics");
        system.restore(after_read); tick(system, 2); check(system.state() == expected, "side-effecting read replay differs");
    }
}
void acknowledge_and_complete_same_tick() {
    System system; prepare(system, 8);
    for (unsigned n = 0; n < 200 && (system.memory().iop().state().cop0.cause & 0x400) == 0; ++n) { tick(system); }
    check(system.memory().hardware().state().iop_intc.status == 8, "first DMA completion missing");
    auto& iop = system.memory().iop();
    Guest next{iop, 0x600};
    next.store(stat, ~8U); // clear controller first, leaving old DICR2 signal high
    next.store(0x1f801520, 0x9000); next.store(0x1f801524, 0x10020);
    next.li(1, 0x1f801574); next.li(2, 0x04040000);
    next.store(0x1f801528, 0x01000201); // new first qword is produced on this tick
    for (unsigned n = 0; n < 6; ++n) { next.emit(0); }
    const auto ack_pc = next.pc;
    next.emit(imm(43, 1, 2, 0)); // acknowledge old DICR2 just before last qword completes
    next.emit(0x1000ffff); next.emit(0);
    auto saved = system.state(); auto& cpu = saved.memory.hardware.iop;
    cpu.pc = 0x600; cpu.next_pc = 0x604; cpu.branch_pc = 0; cpu.delay_slot = false;
    cpu.pending_load.reset(); cpu.cop0.status = 0;
    system.restore(saved);
    for (unsigned n = 0; n < 80 && iop.state().pc != ack_pc; ++n) { tick(system); }
    const auto before = system.state();
    check(iop.state().pc == ack_pc && before.memory.hardware.iop_intc.status == 0 &&
          before.memory.hardware.iop_intc.levels == 8 && before.memory.hardware.sif.iop_send.block_words == 28,
          "fixture must have held old IRQ and one remaining qword");
    tick(system); const auto after = system.state();
    check(after.memory.hardware.iop_intc.status == 8 && after.memory.hardware.iop_intc.levels == 8 &&
          (after.memory.hardware.sif.iop_send.chcr & 0x01000000) == 0,
          "same-tick falling acknowledgement and new rising completion lost");
    system.restore(before); tick(system); check(system.state() == after, "same-tick edge replay differs");
}
void guests() {
    for (auto source : {4U, 8U, 0x20000U}) {
        System system; prepare(system, source);
        for (unsigned n = 0; n < 500 && (system.memory().iop().state().cop0.cause & 0x400) == 0; ++n) { tick(system); }
        const auto pending = system.state();
        check(pending.memory.hardware.iop_intc.status == source &&
              pending.memory.hardware.iop_intc.levels == source &&
              (pending.memory.hardware.iop.cop0.cause & 0x400) != 0,
              "peripheral completion did not latch pending IP2");
        tick(system); check(system.memory().iop().state().pc == 0x80000080, "guest interrupt vector");
        tick(system); const auto handler = system.state();
        check(handler.memory.hardware.iop.pending_load &&
              handler.memory.hardware.iop.pending_load->reg == 26, "handler EPC load checkpoint");
        tick(system, 100); const auto expected = system.state();
        check(system.memory().iop().read32(0x1000) == 1 &&
              (system.memory().iop().read32(0x1100) & 0x47c) == 0x400,
              "guest handler service count or interrupt cause");
        check(expected.memory.hardware.iop_intc.status == 0 && expected.memory.hardware.iop_intc.levels == 0 &&
              (expected.memory.hardware.iop.cop0.cause & 0x400) == 0 &&
              (expected.memory.hardware.iop.cop0.status & 0x401) == 0x401,
              "guest acknowledgements and RFE did not restore idle execution");
        if (source == 4) { check(system.memory().iop().read32(0x9000) == 0x5a5a5a5a, "CDVD sector DMA bytes"); }
        if (source == 8) { check(system.memory().read(0xa07c, 4) == 0x1234001f, "SIF DMA final word"); }
        if (source == 0x20000) {
            check(system.memory().sio2().state().output == std::vector<std::uint8_t>({0xff, 0x41, 0x5a, 0xff, 0xff}), "SIO2 pad reply");
        }
        system.restore(handler); tick(system, 37); tick(system, 63);
        check(system.state() == expected, "in-handler partitioned replay differs");
        system.restore(pending); std::vector<InstructionTrace> first, second;
        tick(system, 102, &first); check(system.state() == expected, "pending replay state differs");
        system.restore(pending); tick(system, 41, &second); tick(system, 61, &second);
        check(system.state() == expected && first == second, "pending partitioned state/trace replay differs");
        auto invalid = expected; invalid.memory.hardware.iop_intc.levels |= 0x80000000;
        rejects([&] { system.restore(invalid); }); check(system.state() == expected, "bad integrated snapshot changed state");
    }
}
}
int main() {
    try { registers(); gating_and_control_reads(); guests(); acknowledge_and_complete_same_tick(); std::cout << "IOP interrupt controller tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
