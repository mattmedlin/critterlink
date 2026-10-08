#include "critterlink/system.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace critterlink;
constexpr std::uint32_t chcr = 0x10009000, madr = 0x10009010, qwc = 0x10009020;
constexpr std::uint32_t tadr = 0x10009030, asr0 = 0x10009040, asr1 = 0x10009050;
constexpr std::uint32_t ctrl = 0x1000e000, stat = 0x1000e010;

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template<class T> auto snap(const T& object) {
    using S = std::remove_cvref_t<decltype(object.state())>;
    return std::unique_ptr<S>(new S(object.state()));
}

template<class F> void rejects(F fn) {
    try { fn(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("missing rejection");
}

struct Fixture {
    std::unique_ptr<Hardware> hardware = std::make_unique<Hardware>();
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x4000);
    Hardware& h() { return *hardware; }
    void word(std::uint32_t at, std::uint32_t value) {
        for (unsigned n = 0; n < 4; ++n) {
            ram.at(at + n) = static_cast<std::uint8_t>(value >> (8 * n));
        }
    }
    void words(std::uint32_t at, std::array<std::uint32_t, 4> values) {
        for (unsigned n = 0; n < 4; ++n) word(at + n * 4, values[n]);
    }
    void tag(std::uint32_t at, unsigned id, unsigned count = 0,
             std::uint32_t address = 0, bool irq = false,
             std::uint32_t upper0 = 0, std::uint32_t upper1 = 0) {
        words(at, {(id << 28) | count | (irq ? 0x80000000U : 0U), address, upper0, upper1});
    }
    void start(std::uint32_t control = 0x185, std::uint32_t address = 0x100) {
        h().write(ctrl, 1);
        h().write(tadr, address);
        h().write(chcr, control);
    }
    void advance(unsigned ticks) { h().advance(ticks, ram); }
};

void literal_tags() {
    for (unsigned id = 0; id < 8; ++id) {
        Fixture f;
        const bool reference = id == 0 || id == 3 || id == 4;
        f.tag(0x100, id, 1, reference ? 0x300 : 0x200, false, 0xffffffff, 0xffffffff);
        f.words(reference ? 0x300 : 0x110, {0, 0, 0, 0});
        f.tag(id == 2 || id == 5 ? 0x200 : reference ? 0x110 : 0x120, 7);
        f.start();
        f.advance(1);
        const auto fetched = snap(f.h());
        check(fetched->vif_dma.phase == VifChainPhase::payload &&
              fetched->vif_dma.qwords == 1 && !fetched->vif_dma.loaded,
              "tag fetch consumed payload or ignored count");
        check((f.h().read(chcr) & 0x70000000U) == id << 28, "literal TAG mirror");
        f.advance(1);
        check(f.h().read(qwc) == 0 && !f.h().stop(), "TTE0 did not ignore upper64");
        if (id != 0 && id != 6 && id != 7) f.advance(1);
        check(!(f.h().read(chcr) & 0x100) && (f.h().read(stat) & 2), "literal tag completion");
    }
}

void nesting_and_tie() {
    Fixture f;
    f.tag(0x100, 5, 0, 0x200, true);
    f.tag(0x110, 7);
    f.tag(0x200, 5, 0, 0x300);
    f.tag(0x210, 6);
    f.tag(0x300, 6);
    f.start();
    f.advance(1);
    check(f.h().read(tadr) == 0x200 && f.h().read(asr0) == 0x110 &&
          (f.h().read(chcr) & 0x130) == 0x10, "CALL TIE boundary");
    const auto tied = snap(f.h());
    f.h().write(stat, 2);
    f.h().write(chcr, f.h().read(chcr) | 0x100);
    f.advance(1);
    check(f.h().read(asr1) == 0x210 && (f.h().read(chcr) & 0x30) == 0x20,
          "resume repeated CALL or failed nested CALL");
    const auto nested = snap(f.h());
    f.advance(3);
    const auto done = snap(f.h());
    check(!(f.h().read(chcr) & 0x130) && !f.h().stop(), "nested RET sequence");
    f.h().restore(*nested);
    f.advance(3);
    check(*snap(f.h()) == *done, "nested stack replay");
    f.h().restore(*tied);
    f.h().write(qwc, 0);
    rejects([&] { f.h().write(chcr, tied->vif_dma.chcr | 0x100); });
    for (unsigned id : {0U, 6U, 7U}) {
        Fixture terminal;
        terminal.tag(0x100, id, 0, 0, true);
        terminal.start();
        terminal.advance(1);
        check(snap(terminal.h())->vif_dma.phase == VifChainPhase::idle,
              "terminal IRQ became resumable TIE");
    }
    Fixture overflow;
    overflow.tag(0x100, 5, 0, 0x200);
    overflow.tag(0x200, 5, 0, 0x300);
    overflow.tag(0x300, 5, 0, 0x400, false, 0xffffffff, 0xffffffff);
    overflow.start(0x1c5);
    overflow.advance(5);
    check(!overflow.h().stop() && !(overflow.h().read(chcr) & 0x100),
          "overflow CALL forwarded forbidden upper words");
    Fixture loop;
    loop.tag(0x100, 2, 0, 0x100);
    loop.start();
    loop.advance(30);
    check(loop.h().now() == 30 && (loop.h().read(chcr) & 0x100), "zero QWC loop unbounded");
}

void tag_transport_and_parser() {
    Fixture f;
    // UNPACK command and its first datum live in the tag upper64. QWC excludes them.
    f.tag(0x100, 7, 1, 0, false, 0x6c010005, 11);
    f.words(0x110, {22, 33, 44, 0});
    f.start(0x1c5);
    f.advance(1);
    const auto fetched = snap(f.h());
    check(fetched->vif_dma.phase == VifChainPhase::tag_words && f.h().read(qwc) == 1,
          "TTE fetch did not retain upper words");
    f.advance(1);
    check(f.h().read(qwc) == 1 && f.h().read(madr) == 0x110 &&
          f.h().vector().payload_remaining == 3, "tag words counted as payload qword");
    f.advance(1);
    check(f.h().vector().data[5] == std::array<std::uint32_t, 4>{11, 22, 33, 44},
          "TTE word order or parser continuation");
    const auto done = snap(f.h());
    f.h().restore(*fetched);
    f.advance(2);
    check(*snap(f.h()) == *done, "midtag replay");

    Fixture spanning;
    spanning.tag(0x100, 1, 0, 0, false, 0x6c010006, 101);
    spanning.tag(0x110, 7, 1, 0, false, 202, 303);
    spanning.words(0x120, {404, 0, 0, 0});
    spanning.start(0x1c5);
    spanning.advance(5);
    check(!spanning.h().stop() && spanning.h().vector().data[6] ==
          std::array<std::uint32_t, 4>{101, 202, 303, 404}, "parser reset at DMA tag");

    Fixture mpg;
    mpg.tag(0x100, 7, 1, 0, false, 0, 0x4a020000);
    mpg.words(0x110, {0x10000000, 0x400002ff, 0x10000000, 0x000002ff});
    mpg.start(0x1c5);
    mpg.advance(3);
    check(!mpg.h().stop() && mpg.h().vector().micro[0] ==
          std::array<std::uint32_t, 2>{0x10000000, 0x400002ff}, "tag lane3 MPG alignment");
    Fixture bad;
    bad.tag(0x100, 7, 0, 0, false, 0x4a020000, 0);
    bad.start(0x1c5);
    bad.advance(2);
    check(bad.h().stop() && bad.h().vector().payload == 0 &&
          snap(bad.h())->vif_dma.tag_cursor == 0, "tag lane2 MPG consumed before rejection");
}

void stalls_and_dma_pause() {
    for (unsigned lane : {0U, 1U, 2U}) {
        Fixture f;
        auto seeded = snap(f.h());
        // Deterministic backpressure fixture: valid long NOP program, end at instruction4.
        for (unsigned n = 0; n < 6; ++n) seeded->vector.micro[n] = {0x10000000, 0x000002ff};
        seeded->vector.micro[4][1] |= 0x40000000;
        seeded->vector.running = true;
        f.h().restore(*seeded);
        if (lane < 2) {
            f.tag(0x100, 7, 0, 0, false, lane == 0 ? 0x10000000 : 0,
                  lane == 1 ? 0x10000000 : 0);
        } else {
            f.tag(0x100, 7, 1);
            f.words(0x110, {0, 0, 0, 0x10000000});
        }
        f.start(lane < 2 ? 0x1c5 : 0x185);
        f.advance(2);
        const auto stalled = snap(f.h());
        check(lane < 2 ? stalled->vif_dma.tag_cursor == lane :
              stalled->vif_dma.loaded && stalled->vif_dma.cursor == 3, "partial word stall cursor");
        f.word(lane < 2 ? 0x108 + lane * 4 : 0x11c, 0xffffffff);
        f.h().write(ctrl, 0);
        f.advance(4);
        check(!f.h().vector().running && (f.h().read(chcr) & 0x100), "DMAE failed to preserve stall");
        f.h().write(ctrl, 1);
        f.advance(1);
        check(!f.h().stop() && !(f.h().read(chcr) & 0x100), "latched words reread after stall");
        const auto done = snap(f.h());
        f.h().restore(*stalled);
        f.h().write(ctrl, 0);
        f.advance(4);
        f.h().write(ctrl, 1);
        f.advance(1);
        check(*snap(f.h()) == *done, "partial word replay");
    }
}

void preload_tte_and_stalled_tie() {
    Fixture preload;
    // A preloaded CNT starts with data; no fetched tag exists to supply upper64.
    preload.words(0x300, {0, 0, 0, 0x6c010007});
    preload.tag(0x100, 7, 1, 0, false, 11, 22);
    preload.words(0x110, {33, 44, 0, 0});
    preload.h().write(madr, 0x300);
    preload.h().write(qwc, 1);
    preload.start(0x100001c5);
    preload.advance(1);
    check(preload.h().read(qwc) == 0 && preload.h().read(madr) == 0x310 &&
          preload.h().vector().payload_remaining == 4 &&
          snap(preload.h())->vif_dma.phase == VifChainPhase::tag,
          "preloaded TTE CNT invented tag words or skipped initial payload");
    preload.advance(3);
    check(!preload.h().stop() && preload.h().vector().data[7] ==
          std::array<std::uint32_t, 4>{11, 22, 33, 44},
          "preloaded CNT did not continue through fetched upper64");

    for (bool tag_stall : {false, true}) {
        Fixture f;
        auto seeded = snap(f.h());
        seeded->vector.micro[0] = {0x10010801, 0x400002ff}; // vi1 += 1, E.
        seeded->vector.micro[1] = {0x10000000, 0x000002ff};
        f.h().restore(*seeded);
        const auto following = tag_stall ? 0x110U : 0x120U;
        if (tag_stall) {
            f.tag(0x100, 1, 0, 0, true, 0x14000000, 0x10000000);
        } else {
            f.tag(0x100, 1, 1, 0, true);
            f.words(0x110, {0x14000000, 0, 0, 0x10000000});
        }
        f.tag(following, 7);
        f.start(tag_stall ? 0x1c5 : 0x185);
        f.advance(2);
        check((f.h().read(chcr) & 0x100) && !(f.h().read(stat) & 2) &&
              f.h().read(tadr) == 0x100 && f.h().vector().running,
              "TIE fired before the final stalled word was accepted");
        f.advance(1);
        check(!(f.h().read(stat) & 2), "TIE fired during VU end delay");
        f.advance(1);
        check(!(f.h().read(chcr) & 0x100) && (f.h().read(stat) & 2) &&
              f.h().read(tadr) == following && f.h().vector().vi[1] == 1,
              "TIE failed to publish completed packet boundary");
        f.h().write(stat, 2);
        f.h().write(chcr, f.h().read(chcr) | 0x100);
        f.advance(tag_stall ? 2 : 1);
        check(!f.h().stop() && !(f.h().read(chcr) & 0x100) &&
              f.h().vector().vi[1] == 1 && !f.h().vector().running,
              "TIE continuation forwarded completed words a second time");
    }
}

void errors_and_snapshots() {
    for (bool data_error : {false, true}) {
        Fixture f;
        f.tag(0x100, 0, 1, 0x4000);
        f.start(0x185, data_error ? 0x100 : 0x4000);
        f.advance(data_error ? 2 : 1);
        const auto faulted = snap(f.h());
        check(f.h().stop() && (f.h().read(stat) & 0x8002) == 0x8002 &&
              faulted->vif_dma.phase == VifChainPhase::idle && !faulted->vif_dma.loaded &&
              faulted->vif_dma.tag_cursor == 0, "bus error not normalized");
        f.h().restore(*faulted);
        check(*snap(f.h()) == *faulted, "faulted snapshot rejected");
    }
    Fixture unsupported;
    unsupported.tag(0x100, 5, 0, 0x200);
    unsupported.words(0x200, {0x74000000, 0, 0, 0});
    unsupported.start();
    unsupported.advance(1);
    const auto before = snap(unsupported.h());
    unsupported.advance(1);
    check(unsupported.h().stop() && snap(unsupported.h())->vif_dma == before->vif_dma,
          "unsupported tag partially committed");

    Fixture valid;
    valid.tag(0x100, 7, 1);
    valid.start(0x1c5);
    valid.advance(1);
    const auto original = snap(valid.h());
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
        auto bad = std::unique_ptr<HardwareState>(new HardwareState(*original));
        if (mutation == 0) bad->vif_dma.tag_cursor = 2;
        if (mutation == 1) bad->vif_dma.loaded = true;
        if (mutation == 2) bad->vif_dma.chcr &= ~0x40U;
        if (mutation == 3) bad->vif_dma.asr[0] = 1;
        if (mutation == 4) bad->vif_dma.phase = VifChainPhase::idle;
        rejects([&] { valid.h().restore(*bad); });
        check(*snap(valid.h()) == *original, "invalid snapshot changed hardware");
    }
    // A completed terminal packet cannot be represented as a pending tag fetch.
    for (bool running : {false, true}) {
        auto bad = std::unique_ptr<HardwareState>(new HardwareState(*original));
        bad->vif_dma = {};
        bad->vif_dma.phase = VifChainPhase::tag;
        bad->vif_dma.chcr = 0xf0000085U | (running ? 0x100U : 0U);
        rejects([&] { valid.h().restore(*bad); });
        bad->vif_dma = {};
        bad->dma.phase = GifChainPhase::tag;
        bad->dma.chcr = 0xf0000085U | (running ? 0x100U : 0U);
        rejects([&] { valid.h().restore(*bad); });
    }
    auto stopped_refe = std::unique_ptr<HardwareState>(new HardwareState(*original));
    stopped_refe->vif_dma = {};
    stopped_refe->vif_dma.phase = VifChainPhase::tag;
    stopped_refe->vif_dma.chcr = 0x80000085;
    rejects([&] { valid.h().restore(*stopped_refe); });
    check(*snap(valid.h()) == *original, "terminal fetch snapshot mutated hardware");
    rejects([&] { valid.h().write(tadr, 0x200); });
    rejects([&] { valid.h().write(asr0, 0x200); });
    valid.h().write(chcr, 0);
    check(snap(valid.h())->vif_dma.phase == VifChainPhase::idle, "abort retained chain transport");
}

void guest_and_replay() {
    auto system = std::make_unique<System>();
    auto& s = *system;
    auto& m = s.memory();
    m.write_quadword(0x2000, {0x70000002, 0x4a02000000000000ULL});
    m.write_quadword(0x2010, {0x400002ff10010007ULL, 0x000002ff10000000ULL});
    m.write_quadword(0x2020, {0x1000000014000000ULL, 0});
    constexpr std::uint32_t guest[] {
        0x3c081001, 0x34090001, 0xad09e000, 0x3c090002, 0xad09e010,
        0x34092000, 0xad099030, 0x340901c5, 0xad099000, 0x1000ffff, 0
    };
    constexpr std::uint32_t handler[] {
        0x3c081001, 0x8d09e010, 0xac091000, 0x34090002, 0xad09e010,
        0x26100001, 0x42000018
    };
    for (unsigned n = 0; n < std::size(guest); ++n) m.write(n * 4, 4, guest[n]);
    for (unsigned n = 0; n < std::size(handler); ++n) m.write(0x200 + n * 4, 4, handler[n]);
    auto cpu = snap(s.cpu());
    cpu->cop0.status = 0x10801;
    s.cpu().restore(*cpu);
    check(s.run(9).budget_exhausted, "guest launch");
    const auto pending = snap(s);
    std::vector<InstructionTrace> a, b;
    check(s.run(35, &a).budget_exhausted && !m.hardware().stop() &&
          m.hardware().vector().vi[1] == 7 && s.cpu().state().gpr[16].low == 1 &&
          (m.read(0x1000, 4) & 2) && !m.hardware().int1(), "guest VIF chain and INT1 handler");
    const auto done = snap(s);
    s.restore(*pending);
    check(s.run(13, &b).budget_exhausted && s.run(22, &b).budget_exhausted &&
          *snap(s) == *done && a == b, "guest VIF chain replay");
}
}

int main() {
    try {
        literal_tags();
        nesting_and_tie();
        tag_transport_and_parser();
        stalls_and_dma_pause();
        preload_tte_and_stalled_tie();
        errors_and_snapshots();
        guest_and_replay();
        std::cout << "VIF source chain integration tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
