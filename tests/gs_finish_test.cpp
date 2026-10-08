#include "critterlink/system.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace critterlink;
constexpr std::uint32_t csr = 0x12001000, imr = 0x12001010;
constexpr std::uint32_t intc_stat = 0x1000f000, intc_mask = 0x1000f010;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class T> auto snap(const T& object) {
    using S = std::remove_cvref_t<decltype(object.state())>;
    return std::unique_ptr<S>(new S(object.state()));
}
template<class E, class F> void rejects(F fn) {
    try { fn(); }
    catch (const E&) { return; }
    throw std::runtime_error("missing rejection");
}
void command(Memory& m, unsigned address, std::uint64_t value = 0) {
    m.write_quadword(0x10006000, {0x1000000000008001ULL, 14});
    m.write_quadword(0x10006000, {value, address});
    m.advance(2);
}
void transfer(Memory& m, bool copy = false) {
    command(m, 0x50, (1ULL << 16) | (1ULL << 32) | (1ULL << 48));
    command(m, 0x51);
    command(m, 0x52, (1ULL << 32) | 8);
    command(m, 0x53, copy ? 2 : 0);
}

void access_and_masked_latch() {
    auto memory = std::make_unique<Memory>();
    auto& m = *memory;
    check(m.read(csr, 8) == 0 && m.hardware().graphics().imr == 0x1f00,
          "initial diagnostic CSR and IMR masks");
    for (unsigned width : {1U, 2U, 4U}) {
        rejects<MemoryFault>([&] { (void)m.read(csr, width); });
        rejects<MemoryFault>([&] { m.write(csr, width, 2); });
    }
    rejects<MemoryFault>([&] { (void)m.read_quadword(csr); });
    rejects<MemoryFault>([&] { m.write_quadword(csr, {2, 0}); });
    rejects<MemoryFault>([&] { (void)m.read(csr + 1, 8); });
    rejects<MemoryFault>([&] { (void)m.read(imr, 8); });
    rejects<MemoryFault>([&] { m.write(0x12001040, 8, 1); });
    rejects<MemoryFault>([&] { (void)m.read(0x12001080, 8); });
    rejects<MemoryFault>([&] { (void)m.read(0x12001400, 8); });
    command(m, 0x61, 0xffffffffffffffffULL);
    check(m.read(0x92001000, 8) == 2 && m.read(0xb2001000, 8) == 2 &&
          m.read(intc_stat, 4) == 0, "masked FINISH did not latch through segment aliases");
    const auto masked = snap(m);
    m.write(imr, 8, 0xfffffffffffffdffULL);
    check(m.read(intc_stat, 4) == 1 && !m.hardware().int0(), "unmask failed falling edge");
    const auto asserted_line = snap(m);
    m.restore(*asserted_line);
    check(*snap(m) == *asserted_line, "asserted GS line restore changed edge state");
    m.write(intc_mask, 4, 1);
    check(m.hardware().int0(), "INTC mask independent from GS mask");
    m.write(intc_stat, 4, 1);
    m.advance(5);
    command(m, 0x61);
    check(m.read(csr, 8) == 2 && m.read(intc_stat, 4) == 0,
          "unchanged asserted GS line repeatedly latched INTC");
    m.write(csr, 8, 0);
    check(m.read(csr, 8) == 2, "CSR zero acknowledged FINISH");
    m.write(csr, 8, 0xfffff002ULL);
    check(m.read(csr, 8) == 0, "CSR metadata bits prevented W1C");
    command(m, 0x61);
    check(m.read(intc_stat, 4) == 1, "acknowledged FINISH did not generate next edge");
    m.write(csr, 8, 2);
    check(m.read(intc_stat, 4) == 1, "GS ack incorrectly acknowledged INTC");
    const auto completed = snap(m);
    m.restore(*masked);
    m.write(imr, 8, 0xfffffffffffffdffULL);
    m.write(intc_mask, 4, 1);
    m.write(intc_stat, 4, 1);
    m.advance(5);
    command(m, 0x61);
    m.write(csr, 8, 0);
    m.write(csr, 8, 0xfffff002ULL);
    command(m, 0x61);
    m.write(csr, 8, 2);
    check(*snap(m) == *completed, "masked event replay differs");
    const auto latched = snap(m);
    m.restore(*latched);
    check(*snap(m) == *latched, "latched INTC replay introduced edge");
    for (std::uint64_t invalid : {1ULL, 4ULL, 0x100ULL, 0x200ULL, 0x20ULL}) {
        const auto before = snap(m);
        rejects<MemoryFault>([&] { m.write(csr, 8, invalid | 2); });
        check(*snap(m) == *before, "invalid CSR write partially acknowledged event");
    }
}

void fences_and_replacement() {
    auto memory = std::make_unique<Memory>();
    auto& m = *memory;
    transfer(m);
    command(m, 0x61);
    command(m, 0x61);
    check(m.hardware().graphics().finish_pending && m.read(csr, 8) == 0,
          "unfinished upload FINISH was premature");
    const auto pending = snap(m);
    m.write_quadword(0x10006000, {0x0800000000008002ULL, 0});
    m.write_quadword(0x10006000, {0x0000000200000001ULL, 0x0000000400000003ULL});
    m.write_quadword(0x10006000, {0x0000000600000005ULL, 0x0000000800000007ULL});
    m.advance(3);
    check(m.read(csr, 8) == 2 && !m.hardware().graphics().finish_pending &&
          m.hardware().graphics().vram[77] == 8, "pending FINISH blocked following IMAGE");
    const auto done = snap(m);
    m.restore(*pending);
    m.write_quadword(0x10006000, {0x0800000000008002ULL, 0});
    m.write_quadword(0x10006000, {0x0000000200000001ULL, 0x0000000400000003ULL});
    m.write_quadword(0x10006000, {0x0000000600000005ULL, 0x0000000800000007ULL});
    m.advance(1);
    m.advance(2);
    check(*snap(m) == *done, "precompletion fence replay");
    for (unsigned replacement : {0U, 2U, 3U}) {
        m.write(csr, 8, 2);
        transfer(m);
        command(m, 0x61);
        command(m, 0x53, replacement);
        check(m.read(csr, 8) == 2 && !m.hardware().graphics().finish_pending,
              "fence followed replacement instead of its original transfer");
        command(m, 0x53, 3);
    }
    m.write(csr, 8, 2);
    transfer(m, true);
    command(m, 0x61);
    check(m.read(csr, 8) == 0 && m.hardware().graphics().finish_pending,
          "FINISH did not wait for active copy policy");
    m.advance(6);
    check(m.read(csr, 8) == 2, "copy completion failed pending FINISH");
    m.write(csr, 8, 2);
    transfer(m, true);
    m.advance(8);
    check(m.read(csr, 8) == 0, "ordinary copy invented FINISH event");
}

void invalid_snapshots() {
    auto h = std::make_unique<Hardware>();
    const auto initial = snap(*h);
    for (unsigned mutation = 0; mutation < 3; ++mutation) {
        auto bad = std::unique_ptr<HardwareState>(new HardwareState(*initial));
        if (mutation == 0) bad->graphics.imr = 1;
        if (mutation == 1) bad->graphics.finish_pending = true;
        if (mutation == 2) bad->gs_interrupt_high = false;
        rejects<std::invalid_argument>([&] { h->restore(*bad); });
        check(*snap(*h) == *initial, "invalid fence/mask/line restore changed hardware");
    }
}

void original_guest_irq() {
    auto system = std::make_unique<System>();
    auto& s = *system;
    auto& m = s.memory();
    m.write_quadword(0x3000, {0x1000000000008001ULL, 14});
    m.write_quadword(0x3010, {0, 0x61});
    // Original guest unmasks FINISH using SD, enables INTC0, and emits FINISH.
    constexpr std::uint32_t guest[] {
        0x3c081200, 0x34091d00, 0xfd091010,
        0x3c081001, 0x34090001, 0xad09f010,
        0x3c081000, 0x35086000, 0x34093000,
        0x792a0000, 0x7d0a0000, 0x792a0010, 0x7d0a0000, 0x1000ffff, 0
    };
    // LD CSR, preserve observed value, SD W1C, then independent INTC W1C and ERET.
    constexpr std::uint32_t handler[] {
        0x3c0a1200, 0xdd4b1000, 0xfc0b1000, 0x354a1000, 0xfd4b0000,
        0x3c0a1001, 0x340b0001, 0xad4bf000, 0x26100001, 0x42000018
    };
    for (unsigned n = 0; n < std::size(guest); ++n) m.write(n * 4, 4, guest[n]);
    for (unsigned n = 0; n < std::size(handler); ++n) m.write(0x200 + n * 4, 4, handler[n]);
    auto cpu = snap(s.cpu());
    cpu->cop0.status = 0x10401;
    s.cpu().restore(*cpu);
    check(s.run(12).budget_exhausted, "FINISH guest setup");
    const auto pending = snap(s);
    std::vector<InstructionTrace> a, b;
    check(s.run(40, &a).budget_exhausted && !m.hardware().stop() &&
          s.cpu().state().gpr[16].low == 1 && m.read(0x1000, 8) == 2 &&
          m.read(csr, 8) == 0 && m.read(intc_stat, 4) == 0,
          "guest LD/SD FINISH handler service count");
    const auto done = snap(s);
    s.restore(*pending);
    check(s.run(17, &b).budget_exhausted && s.run(23, &b).budget_exhausted &&
          *snap(s) == *done && a == b, "FINISH guest state and trace replay");
}
}
int main() {
    try {
        access_and_masked_latch();
        fences_and_replacement();
        invalid_snapshots();
        original_guest_irq();
        std::cout << "GS FINISH tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
