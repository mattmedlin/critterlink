#include "critterlink/hardware.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using critterlink::Hardware;
constexpr std::uint32_t stat = 0x1000f000, mask = 0x1000f010;
constexpr std::uint32_t chcr = 0x1000a000, madr = 0x1000a010, qwc = 0x1000a020;
constexpr std::uint32_t ctrl = 0x1000e000, dstat = 0x1000e010;
constexpr std::uint32_t timer(unsigned index) { return 0x10000000 + index * 0x800; }
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <class F> void rejected(F function) {
    try { function(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("missing invalid-argument rejection");
}
void test_timers() {
    Hardware h;
    for (unsigned i = 0; i < 4; ++i) {
        h.write(timer(i) + 0x20, 2);
        h.write(timer(i) + 0x10, 0x180);
    }
    h.advance(1, {});
    check(h.read(stat) == 0, "early timer interrupt");
    h.advance(1, {});
    check(h.read(stat) == 0x1e00, "four timer interrupt lines");
    check(!h.int0(), "masked timer interrupt");
    h.write(mask, 0x200);
    check(h.int0(), "enabled timer interrupt");
    h.write(mask, 0x200);
    check(!h.int0() && h.read(mask) == 0, "INTC mask must toggle");
    h.write(stat, 0x200);
    check(h.read(stat) == 0x1c00, "INTC W1C affects selected source only");
    h.write(timer(0), 1);
    h.advance(1, {});
    check(h.read(stat) == 0x1c00, "latched compare must not rearm on INTC acknowledgement");
    h.write(timer(0) + 0x10, 0x580);
    h.write(timer(0), 1);
    h.advance(1, {});
    check(h.read(stat) == 0x1e00, "MODE W1C rearms compare");

    Hardware divided;
    divided.write(timer(0) + 0x10, 0x80);
    divided.write(timer(1) + 0x10, 0x81);
    divided.write(timer(2) + 0x10, 0x82);
    divided.advance(255, {});
    check(divided.read(timer(0)) == 255 && divided.read(timer(1)) == 15 &&
          divided.read(timer(2)) == 0, "timer divisors before boundary");
    divided.advance(1, {});
    check(divided.read(timer(0)) == 256 && divided.read(timer(1)) == 16 &&
          divided.read(timer(2)) == 1 && divided.read(timer(3)) == 0, "divisor boundary and disabled timer");
    // These are the documented diagnostic rearm/prescaler policies, not a
    // hardware-conformance claim about write latency or real bus cycles.
    divided.advance(7, {});
    divided.write(timer(1), 0);
    divided.advance(15, {});
    check(divided.read(timer(1)) == 0, "COUNT write resets phase");
    divided.advance(1, {});
    check(divided.read(timer(1)) == 1, "COUNT write phase boundary");
    divided.advance(7, {});
    divided.write(timer(1) + 0x10, 1);
    divided.advance(40, {});
    divided.write(timer(1) + 0x10, 0x81);
    divided.advance(15, {});
    check(divided.read(timer(1)) == 1, "enable transition resets phase");

    Hardware overflow;
    overflow.write(timer(3), 0xffff);
    overflow.write(timer(3) + 0x10, 0x380);
    overflow.advance(1, {});
    check(overflow.read(timer(3)) == 0 && overflow.read(timer(3) + 0x10) == 0xf80 &&
          overflow.read(stat) == 0x1000, "simultaneous overflow and compare flags");
    overflow.write(timer(3) + 0x10, 0xb80);
    check(overflow.read(timer(3) + 0x10) == 0x780, "overflow W1C preserves compare flag");
    Hardware zero;
    zero.write(timer(0) + 0x20, 3);
    zero.write(timer(0) + 0x10, 0xc0);
    zero.advance(8, {});
    check(zero.read(timer(0)) == 2 && zero.read(stat) == 0, "zero-on-compare without IRQ enable");
}
std::vector<std::uint8_t> sprite() {
    const std::array<std::array<std::uint32_t, 4>, 6> packet{{
        {0x8005, 0x10000000, 14, 0}, {6, 0, 0, 0},
        {0x10000, 0, 0x4c, 0}, {0x80402010, 0, 1, 0},
        {0x00300020, 0, 5, 0}, {0x00700050, 0, 5, 0}}};
    std::vector<std::uint8_t> ram;
    for (const auto& words : packet) for (const auto word : words)
        for (unsigned byte = 0; byte < 4; ++byte)
            ram.push_back(static_cast<std::uint8_t>(word >> (byte * 8)));
    return ram;
}
void start(Hardware& h, std::uint32_t count) {
    h.write(madr, 0); h.write(qwc, count); h.write(chcr, 0x101);
}
void test_dma() {
    auto ram = sprite();
    Hardware h;
    start(h, 6);
    h.advance(3, ram);
    check(h.read(qwc) == 6 && h.read(madr) == 0, "globally disabled DMA must pause");
    h.write(ctrl, 1);
    h.write(dstat, 0x40000);
    h.advance(3, ram);
    check(h.read(qwc) == 3 && h.read(madr) == 48 && !h.int1(), "one qword per logical tick");
    const auto snapshot = h.state();
    h.advance(3, ram);
    const auto expected = h.state();
    check(h.read(qwc) == 0 && h.read(madr) == 96 && h.read(chcr) == 1 && h.int1(), "DMA completion");
    for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x)
        check(h.graphics().pixels[y * 64 + x] ==
              ((x >= 2 && x < 5 && y >= 3 && y < 7) ? 0x80402010u : 0u), "DMA sprite exact pixels");
    h.restore(snapshot);
    h.advance(1, ram); h.advance(2, ram);
    check(h.state() == expected, "mid-DMA snapshot and partitioned replay");
    h.write(dstat, 0x40000);
    check(!h.int1() && h.read(dstat) == 4, "DMA interrupt mask toggle");
    h.write(dstat, 4);
    check(h.read(dstat) == 0, "DMA interrupt flag W1C");
    start(h, 0);
    h.advance(1, {});
    check(h.read(chcr) == 1 && h.read(dstat) == 4 && !h.stop(), "zero-count DMA completes without RAM read");

    Hardware failed;
    failed.write(ctrl, 1); start(failed, 1);
    failed.write(timer(0) + 0x20, 1);
    failed.write(timer(0) + 0x10, 0x180);
    failed.advance(20, std::span(ram).first(15));
    check(failed.now() == 1 && failed.stop().has_value() && failed.read(dstat) == 0x8004 &&
          failed.read(chcr) == 1 && failed.read(qwc) == 1 && failed.read(madr) == 0 && failed.int1(), "source bus-error state");
    check(failed.read(stat) == 0x200, "timer executes before failing DMA on shared tick");
    const auto stopped = failed.state();
    failed.advance(4, ram);
    check(failed.state() == stopped, "stopped hardware must not progress");
    Hardware simultaneous;
    simultaneous.write(ctrl, 1); start(simultaneous, 0);
    simultaneous.write(timer(0) + 0x20, 1);
    simultaneous.write(timer(0) + 0x10, 0x180);
    simultaneous.advance(1, {});
    check(simultaneous.read(stat) == 0x200 && simultaneous.read(dstat) == 4,
          "simultaneous timer and DMA completion");
}
void test_rejections() {
    Hardware h;
    const std::array<std::array<std::uint32_t, 2>, 10> writes{{
        {timer(0) + 0x10, 3}, {timer(1) + 0x10, 4}, {timer(2) + 0x30, 1},
        {mask, 0x8000}, {stat, 0x8000}, {ctrl, 2}, {dstat, 1},
        {madr, 1}, {qwc, 0x10000}, {chcr, 0x105}}};
    for (const auto& write : writes) {
        const auto before = h.state();
        rejected([&] { h.write(write[0], write[1]); });
        check(h.state() == before, "unsupported write mutated hardware");
    }
    start(h, 2);
    const auto good = h.state();
    rejected([&] { h.write(madr, 16); });
    rejected([&] { h.write(qwc, 1); });
    rejected([&] { h.write(chcr, 0x101); });
    check(h.state() == good, "active DMA rejection mutated state");
    auto invalid = good;
    invalid.timers[0].phase = 1;
    rejected([&] { h.restore(invalid); });
    invalid = good; invalid.dma.address = 1;
    rejected([&] { h.restore(invalid); });
    invalid = good; invalid.scheduler.events.clear();
    rejected([&] { h.restore(invalid); });
    invalid = good; invalid.graphics.remaining = 32768;
    rejected([&] { h.restore(invalid); });
    check(h.state() == good, "invalid snapshot mutated hardware");

    Hardware edge;
    auto last = edge.state();
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    last.scheduler.now = maximum - 1;
    last.scheduler.next_sequence = maximum;
    last.scheduler.events = {{maximum, maximum - 1, critterlink::EventType::timer, 0}};
    edge.restore(last);
    bool overflow = false;
    try { edge.advance(2, {}); } catch (const std::overflow_error&) { overflow = true; }
    check(overflow && edge.state() == last, "time overflow mutated hardware");
    edge.advance(1, {});
    check(edge.now() == maximum && edge.state().scheduler.events.empty(), "final logical tick");
    const auto at_end = edge.state();
    edge.restore(at_end);
    edge.advance(0, {});
    check(edge.state() == at_end, "final tick restore/zero advance");
}
}
int main() {
    try {
        test_timers(); test_dma(); test_rejections();
        std::cout << "hardware tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
