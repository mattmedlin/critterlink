#include "critterlink/system.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace critterlink;
using Qword = std::array<std::uint64_t, 2>;
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
void submit(Graphics& g, Qword q) {
    g.submit_qword({static_cast<std::uint32_t>(q[0]), static_cast<std::uint32_t>(q[0] >> 32),
                    static_cast<std::uint32_t>(q[1]), static_cast<std::uint32_t>(q[1] >> 32)});
}
void setup(Graphics& g) {
    submit(g, {0x1000000000008002ULL, 14});
    submit(g, {6, 0});
    submit(g, {0x10000, 0x4c});
}
void tag(Graphics& g, unsigned loops, unsigned nreg, std::uint64_t regs, bool end = true) {
    submit(g, {(std::uint64_t{nreg} << 60) | (1ULL << 58) | (end ? 0x8000ULL : 0) | loops, regs});
}

void odd_count_and_loop_crossing() {
    auto g = std::make_unique<Graphics>();
    setup(*g);
    // NREG3: color, first corner, second corner. PRE/PRIM intentionally hostile and ignored.
    submit(*g, {0x3403c00000008001ULL, 0x551});
    submit(*g, {0x11223344, 0});
    const auto partial = snap(*g);
    check(partial->remaining == 1 && partial->reg_index == 2, "NREG3 low/high progression");
    submit(*g, {0x00200020, 0xffffffffffffffffULL});
    check(g->state().pixels[0] == 0x11223344 && g->state().pixels[65] == 0x11223344 &&
          g->state().vram[3] == 0x11223344 && g->state().pixels[2] == 0 &&
          g->state().remaining == 0 && g->state().nreg == 0 && g->state().regs == 0,
          "odd tail padding or literal sprite extent");
    const auto done = snap(*g);
    g->restore(*partial);
    submit(*g, {0x00200020, 0xffffffffffffffffULL});
    check(g->state() == *done, "odd descriptor-index replay");
    tag(*g, 2, 3, 0x551);
    submit(*g, {0x55667788, 0x00000020});
    submit(*g, {0x00100030, 0xaabbccdd}); // Second loop color is upper64, no padding.
    submit(*g, {0x00100020, 0x00200030});
    check(g->state().pixels[2] == 0x55667788 && g->state().pixels[66] == 0xaabbccdd,
          "NREG3 two-loop boundary inserted padding or flipped lanes");
    submit(*g, {0x1000000000008001ULL, 14});
    submit(*g, {0x12345678, 1});
    check(g->state().rgba == 0x12345678, "completed REGLIST did not parse next PACKED tag");
}

void descriptors_and_bounds() {
    auto g = std::make_unique<Graphics>();
    setup(*g);
    // NREG0 means16: fifteen NOPs followed by RGBAQ at descriptor15.
    tag(*g, 1, 0, 0x1fffffffffffffffULL);
    check(g->state().remaining == 16 && g->state().nreg == 16, "NREG0 decoding");
    for (unsigned n = 0; n < 7; ++n) submit(*g, {0xffffffffffffffffULL, 0xffffffffffffffffULL});
    submit(*g, {0xffffffffffffffffULL, 0x98765432});
    check(g->state().rgba == 0x98765432, "descriptor15 omitted");
    tag(*g, 1, 2, 0xfffffffffffffffeULL);
    submit(*g, {0x6100000000ULL, 0xffffffffffffffffULL});
    check(!g->state().finish_event && g->state().rgba == 0x98765432, "REGLIST e/f interpreted as A+D");
    for (unsigned descriptor : {2U, 4U, 6U, 11U, 13U}) {
        const auto before = snap(*g);
        rejects([&] { tag(*g, 1, 1, descriptor); });
        check(g->state() == *before, "unsupported active descriptor changed tag state");
    }
    const auto empty_before = snap(*g);
    submit(*g, {0xffffffffffff8000ULL, 0xbbbbbbbbbbbbbbbbULL});
    auto empty_expected = std::unique_ptr<GraphicsState>(new GraphicsState(*empty_before));
    empty_expected->gif_eop = true;
    check(g->state() == *empty_expected, "NLOOP0 did not ignore unsupported fields except EOP");
    tag(*g, 32767, 0, 0xffffffffffffffffULL);
    check(g->state().remaining == 524272, "maximum REGLIST item count");
    const auto maximum = snap(*g);
    g->restore(*maximum);
    check(g->state() == *maximum, "maximum REGLIST snapshot rejected");
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
        auto bad = std::unique_ptr<GraphicsState>(new GraphicsState(*maximum));
        if (mutation == 0) bad->nreg = 0;
        if (mutation == 1) bad->reg_index = 16;
        if (mutation == 2) bad->remaining = 524273;
        if (mutation == 3) bad->reg_index = 1;
        if (mutation == 4) bad->regs = 0xfffffffffffffffbULL;
        rejects([&] { g->restore(*bad); });
        check(g->state() == *maximum, "malformed REGLIST snapshot changed state");
    }
}

void failing_upper_half_is_atomic() {
    auto g = std::make_unique<Graphics>();
    setup(*g);
    tag(*g, 1, 2, 0x50); // Lower PRIM, upper XYZ2 with invalid fractional coordinate.
    const auto initial = snap(*g);
    rejects([&] { submit(*g, {6, 1}); });
    check(g->state() == *initial, "valid low PRIM survived invalid upper XYZ2");
    g->reset();
    setup(*g);
    submit(*g, {0x1000000000008002ULL, 14});
    submit(*g, {0xabcdef, 1});
    submit(*g, {0, 5});
    tag(*g, 1, 2, 0x05); // Lower XYZ2 would draw; upper unsupported PRIM must undo it.
    const auto before = snap(*g);
    rejects([&] { submit(*g, {0x00100010, 0x16}); });
    check(g->state() == *before && g->state().pixels[0] == 0 && g->state().vram[0] == 0,
          "upper failure retained lower-half rendering");

    auto h = std::make_unique<Hardware>();
    auto seeded = snap(*h);
    seeded->graphics = *before;
    seeded->gif_owner = 3;
    h->restore(*seeded);
    check(h->write_quadword(0x10006000, {0x00100010, 0, 0x16, 0}), "failing qword enqueue");
    const auto queued = snap(*h);
    h->advance(1, {});
    const auto failed = snap(*h);
    check(h->stop() && failed->graphics == queued->graphics &&
          failed->gif_fifo == queued->gif_fifo && failed->gif_owner == queued->gif_owner,
          "Hardware high-half error committed GS/FIFO/owner effects");
}

void eop_packet_ownership() {
    auto m = std::make_unique<Memory>();
    m->write_quadword(0x10006000, {0x1400000000000001ULL, 1});
    m->write_quadword(0x10006000, {0x11111111, 0xffffffffffffffffULL});
    m->advance(2);
    check(snap(m->hardware())->gif_owner == 3, "REGLIST EOP0 lost packet owner");
    m->write_quadword(0x10005000, {0, 0x5000000200000000ULL});
    m->write_quadword(0x10005000, {0x1400000000008001ULL, 1});
    m->write_quadword(0x10005000, {0x22222222, 0});
    m->advance(3);
    check(m->hardware().graphics().rgba == 0x11111111, "PATH2 preempted REGLIST EOP0 packet");
    m->write_quadword(0x10006000, {0x1400000000008001ULL, 1});
    m->write_quadword(0x10006000, {0x33333333, 0});
    const auto partial = snap(*m);
    m->advance(4);
    check(m->hardware().graphics().rgba == 0x22222222 && !m->hardware().stop(),
          "REGLIST packet-boundary arbitration order");
    const auto done = snap(*m);
    m->restore(*partial);
    m->advance(4);
    check(*snap(*m) == *done, "REGLIST packet ownership replay");
}

void both_paths_and_reset() {
    for (unsigned path : {2U, 3U}) {
        auto m = std::make_unique<Memory>();
        constexpr Qword packet[] {
            {0x1000000000008002ULL, 14}, {6, 0}, {0x10000, 0x4c},
            {0x3400000000008001ULL, 0x551}, {0x12345678, 0},
            {0x00100010, 0xffffffffffffffffULL}
        };
        if (path == 2) m->write_quadword(0x10005000, {0, 0x5000000600000000ULL});
        for (const auto& q : packet) m->write_quadword(path == 2 ? 0x10005000 : 0x10006000, q);
        m->advance(9);
        check(!m->hardware().stop() && m->hardware().graphics().pixels[0] == 0x12345678 &&
              m->hardware().graphics().pixels[1] == 0, "literal REGLIST sprite through PATH2/PATH3");
    }
    auto m = std::make_unique<Memory>();
    m->write_quadword(0x10006000, {0x3400000000000002ULL, 0xff1});
    m->write_quadword(0x10006000, {0x11223344, 0});
    m->advance(2);
    const auto before = snap(m->hardware());
    m->write(0x10003000, 4, 1);
    check(m->hardware().graphics().remaining == 0 && m->hardware().graphics().nreg == 0 &&
          m->hardware().graphics().regs == 0 && m->hardware().graphics().reg_index == 0 &&
          m->hardware().graphics().rgba == before->graphics.rgba,
          "GIF reset failed canonical REGLIST metadata cleanup");
}
}
int main() {
    try {
        odd_count_and_loop_crossing();
        descriptors_and_bounds();
        failing_upper_half_is_atomic();
        eop_packet_ownership();
        both_paths_and_reset();
        std::cout << "GIF REGLIST tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
