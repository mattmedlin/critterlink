#include "critterlink/hardware.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
constexpr std::uint32_t chcr = 0x10009000, madr = 0x10009010, qwc = 0x10009020;
constexpr std::uint32_t ctrl = 0x1000e000, stat = 0x1000e010;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejected(F fn) {
    try { fn(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("missing rejection");
}
void start(critterlink::Hardware& h, std::uint32_t count) {
    h.write(madr, 0); h.write(qwc, count); h.write(chcr, 0x101);
}
void test_transfer() {
    std::array<std::uint8_t, 32> ram{}; // VIF NOP commands.
    critterlink::Hardware h;
    start(h, 2);
    h.advance(1, ram);
    check(h.read(qwc) == 2, "DMA disabled must pause VIF1");
    h.write(ctrl, 1);
    h.write(stat, 0x20000);
    h.advance(1, ram);
    check(h.read(qwc) == 1 && h.read(madr) == 16 && !h.int1(), "one qword per tick");
    const auto snapshot = h.state();
    h.advance(1, ram);
    check(h.read(qwc) == 0 && h.read(madr) == 32 && h.read(chcr) == 1 && h.int1(), "VIF1 completion IRQ");
    const auto final = h.state();
    h.restore(snapshot); h.advance(1, ram);
    check(h.state() == final, "DMA restore replay");
    h.write(stat, 2);
    check(!h.int1() && h.read(stat) == 0x20000, "channel1 W1C preserves mask");
    h.write(stat, 0x20000);
    check(h.read(stat) == 0, "channel1 mask toggles");
    start(h, 0); h.advance(1, ram);
    check(h.read(chcr) == 1 && h.read(stat) == 2, "zero count completes without RAM read");
}
void test_independent_channels() {
    critterlink::Hardware h;
    std::array<std::uint8_t, 32> ram{};
    // Empty PACKED GIF tag: NREG=1, A+D descriptor, no payload.
    ram[23] = 0x10; ram[24] = 0x0e;
    h.write(ctrl, 1); h.write(stat, 0x60000);
    start(h, 1);
    h.write(0x1000a010, 16); h.write(0x1000a020, 1); h.write(0x1000a000, 0x101);
    h.advance(1, ram);
    check(!h.stop() && h.read(stat) == 0x60006 && h.int1(), "simultaneous channels complete independently");
    h.write(stat, 2);
    check(h.int1() && h.read(stat) == 0x60004, "clearing VIF IRQ preserves GIF IRQ");
    h.write(stat, 4);
    check(!h.int1(), "both completion flags clear");
    start(h, 2); h.write(chcr, 0);
    h.advance(1, ram);
    check(h.read(qwc) == 2 && h.read(madr) == 0, "stopped channel does not consume");
    h.write(chcr, 0x101); h.advance(1, ram);
    check(h.read(qwc) == 1 && h.read(madr) == 16, "stopped channel can restart");
}
void test_stall_and_resume() {
    std::array<std::uint8_t, 48> ram{};
    const std::array<std::uint32_t, 8> words{
        0, 0x4a020000, 0x10000000, 0x400002ff,
        0x10000000, 0x000002ff, 0x14000000, 0x10000000};
    for (std::size_t n = 0; n < words.size(); ++n) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            ram[n * 4 + byte] = static_cast<std::uint8_t>(words[n] >> (byte * 8));
        }
    }
    critterlink::Hardware h;
    h.write(ctrl, 1); start(h, 2); h.advance(2, ram);
    const auto snapshot = h.state();
    check(!h.stop() && snapshot.vif_dma.loaded && snapshot.vif_dma.cursor == 3 &&
          h.read(madr) == 16 && h.read(qwc) == 1 && h.vector().running,
          "FLUSHE stalls within latched qword after MSCAL");
    // The latched qword must survive a host RAM change and snapshot restoration.
    ram[31] = 0x7f;
    h.advance(1, ram);
    check(h.state().vif_dma.cursor == 3 && h.vector().running, "end bit delay still stalls FLUSHE");
    h.advance(1, ram);
    check(!h.stop() && !h.vector().running && h.read(qwc) == 0, "FLUSHE resumes after end delay");
    const auto final = h.state();
    h.restore(snapshot); h.advance(2, ram);
    check(h.state() == final, "midword stalled DMA restore replay");
    h.restore(snapshot); h.write(ctrl, 0); h.advance(2, ram);
    check(!h.vector().running && h.read(qwc) == 1 && h.state().vif_dma.cursor == 3,
          "disabled DMA preserves partial qword while VU continues");
    h.write(ctrl, 1); h.advance(1, ram);
    check(!h.stop() && h.read(qwc) == 0, "reenabled DMA resumes preserved word");
    h.restore(snapshot); h.write(chcr, 0);
    check(!h.state().vif_dma.loaded && h.state().vif_dma.cursor == 0 && h.vector().running,
          "DMA stop discards buffer and retains VU execution");
    // Explicitly replace the cancelled stream with its unconsumed continuation.
    ram[35] = 0x10; // FLUSHE, then three NOPs.
    h.write(madr, 32); h.write(qwc, 1); h.write(chcr, 0x101);
    h.advance(2, ram);
    check(!h.stop() && !h.vector().running && h.read(qwc) == 0 && h.read(madr) == 48,
          "DMA cancellation restart consumes supplied continuation once");
}
void test_rejections() {
    critterlink::Hardware h;
    rejected([&] { h.write(madr, 1); });
    rejected([&] { h.write(madr, 0x80000000); });
    rejected([&] { h.write(qwc, 0x10000); });
    rejected([&] { h.write(chcr, 0x109); });
    start(h, 1);
    rejected([&] { h.write(madr, 16); });
    rejected([&] { h.write(qwc, 2); });
    rejected([&] { h.write(chcr, 0x101); });
    const auto original = h.state();
    auto bad = original; bad.vif_dma.cursor = 4;
    rejected([&] { h.restore(bad); });
    check(h.state() == original, "bad snapshot changed hardware");
    bad = original; bad.vif_dma.pending[0] = 1;
    rejected([&] { h.restore(bad); });
    bad = original; bad.vif_dma.loaded = true; bad.vif_dma.qwords = 0;
    rejected([&] { h.restore(bad); });
    bad = original; bad.vif_dma.loaded = true; bad.vif_dma.chcr = 0;
    rejected([&] { h.restore(bad); });
    check(h.state() == original, "invalid buffered snapshot changed hardware");
    h.write(ctrl, 1); h.advance(1, {});
    check(h.stop().has_value() && (h.read(stat) & 0x8002u) == 0x8002u && h.int1(), "VIF1 source bounds bus error");
    check(h.read(madr) == 0 && h.read(qwc) == 1, "bounds failure consumed qword");
    critterlink::Hardware unsupported;
    std::array<std::uint8_t, 16> ram{};
    ram[3] = 0x7f;
    unsupported.write(ctrl, 1); start(unsupported, 1); unsupported.advance(1, ram);
    check(unsupported.stop().has_value() && unsupported.read(qwc) == 1 &&
          unsupported.state().vif_dma.cursor == 0, "unsupported VIF command consumed");
    critterlink::Hardware unaligned;
    ram = {}; ram[2] = 2; ram[3] = 0x4a; // MPG at word 0 would put payload at byte 4.
    unaligned.write(ctrl, 1); start(unaligned, 1); unaligned.advance(1, ram);
    check(unaligned.stop().has_value() && unaligned.vector().payload == 0 &&
          unaligned.state().vif_dma.cursor == 0, "misaligned MPG must reject before consuming command");
    critterlink::Hardware bad_micro;
    auto invalid_program = bad_micro.state();
    invalid_program.vector.running = true;
    bad_micro.restore(invalid_program); // Zero instruction pair is unsupported, not a NOP.
    bad_micro.advance(1, {});
    check(bad_micro.stop().has_value() && bad_micro.now() == 1, "unsupported microinstruction must stop hardware");
    const auto stopped = bad_micro.state();
    bad_micro.restore(stopped);
    check(bad_micro.state() == stopped, "microinstruction failure leaves restorable scheduler");
}
}
int main() {
    try { test_transfer(); test_independent_channels(); test_stall_and_resume(); test_rejections(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "VIF1 DMA tests passed\n";
}
