#include "critterlink/system.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace critterlink;
using Words = std::array<std::uint32_t, 4>;
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
void tag(Graphics& g, unsigned loops, unsigned nreg, std::uint64_t regs,
         unsigned mode = 0, bool pre = false, unsigned prim = 0) {
    g.submit_qword({0x8000U | loops, (nreg << 28) | (mode << 26) |
                   (pre ? 0x4000U : 0U) | (prim << 15),
                   static_cast<std::uint32_t>(regs), static_cast<std::uint32_t>(regs >> 32)});
}
void ad(Graphics& g, unsigned address, std::uint64_t value) {
    tag(g, 1, 1, 14);
    g.submit_qword({static_cast<std::uint32_t>(value), static_cast<std::uint32_t>(value >> 32), address, 0});
}

void literal_extraction_and_q() {
    auto g = std::make_unique<Graphics>();
    ad(*g, 0x4c, 0x10000);
    ad(*g, 1, 0x40000000deadbeefULL);
    check(g->state().rgbaq_q == 0x40000000, "A+D RGBAQ Q retention");
    tag(*g, 1, 4, 0x5510);
    g->submit_qword({0xfffff806, 0xffffffff, 0xffffffff, 0xffffffff});
    g->submit_qword({0x12340011, 0x56780022, 0x9abc0033, 0xdef00044});
    check(g->state().rgba == 0x44332211 && g->state().rgbaq_q == 0x3f800000,
          "PACKED lane-byte extraction or GS Q contamination");
    g->submit_qword({0xabcd0010, 0xef120020, 0x12345678, 0xffff7fff});
    const auto partial = snap(*g);
    g->submit_qword({0xffff0030, 0xffff0040, 0xfedcba98, 0x12340001});
    check(g->state().pixels[2 * 64 + 1] == 0x44332211 &&
          g->state().pixels[3 * 64 + 2] == 0x44332211 &&
          g->state().pixels[2 * 64] == 0 && g->state().pixels[4 * 64 + 1] == 0,
          "PACKED low16 XYZ2 literal rectangle");
    const auto done = snap(*g);
    g->restore(*partial);
    g->submit_qword({0xffff0030, 0xffff0040, 0xfedcba98, 0x12340001});
    check(g->state() == *done, "PACKED midlist fullstate replay");
    tag(*g, 1, 1, 1, 1);
    g->submit_qword({0x87654321, 0x40800000, 0xffffffff, 0xffffffff});
    check(g->state().rgbaq_q == 0x40800000, "REGLIST Q retention");
    tag(*g, 1, 1, 0xbbbbbbbbbbbbbbb1ULL);
    g->submit_qword({1, 2, 3, 4});
    check(g->state().rgba == 0x04030201 && g->state().rgbaq_q == 0x3f800000,
          "REGLIST RGBAQ changed unsupported ST-derived internal Q");
}

void wrap_nop_and_bounds() {
    auto g = std::make_unique<Graphics>();
    tag(*g, 2, 3, 0xf1e);
    g->submit_qword({0x11111111, 0x40000000, 1, 0});
    g->submit_qword({0x11, 0x22, 0x33, 0x44});
    g->submit_qword({0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff});
    check(g->state().remaining == 3 && g->state().reg_index == 0, "NREG3 loop wrap");
    g->submit_qword({0x22222222, 0x40800000, 1, 0});
    const auto odd = snap(*g);
    check(odd->reg_index == 1 && odd->remaining == 2, "one descriptor per PACKED qword");
    g->submit_qword({0xaa, 0xbb, 0xcc, 0xdd});
    g->submit_qword({0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff});
    check(g->state().rgba == 0xddccbbaa && g->state().remaining == 0,
          "A+D/NOP/PACKED sequence changed output");
    tag(*g, 1, 0, 0x1fffffffffffffffULL);
    for (unsigned n = 0; n < 15; ++n) g->submit_qword({~0U, ~0U, ~0U, ~0U});
    g->submit_qword({1, 2, 3, 4});
    check(g->state().rgba == 0x04030201 && g->state().remaining == 0,
          "NREG0 descriptor15 did not consume full qword");
    tag(*g, 32767, 0, 0xffffffffffffffffULL);
    check(g->state().remaining == 524272, "PACKED maximum item count");
    g->submit_qword({0, 0, 0, 0});
    const auto maximum_odd = snap(*g);
    check(maximum_odd->reg_index == 1, "PACKED even NREG odd index");
    g->restore(*maximum_odd);
    auto bad = std::unique_ptr<GraphicsState>(new GraphicsState(*maximum_odd));
    bad->mode = GifMode::reglist;
    rejects([&] { g->restore(*bad); });
    check(g->state() == *maximum_odd, "REGLIST parity accepted PACKED odd index");
    for (unsigned mutation = 0; mutation < 3; ++mutation) {
        *bad = *maximum_odd;
        if (mutation == 0) bad->remaining += 1;
        if (mutation == 1) bad->nreg = 17;
        if (mutation == 2) bad->regs = 0xfffffffffffffffbULL;
        rejects([&] { g->restore(*bad); });
        check(g->state() == *maximum_odd, "bad active PACKED metadata was not atomic");
    }
}

void pre_and_adc_atomicity() {
    auto g = std::make_unique<Graphics>();
    ad(*g, 0x4c, 0x10000);
    tag(*g, 1, 2, 0x55, 0, true, 6);
    check(g->state().primitive_ready, "PACKED PRE did not apply primitive");
    g->submit_qword({0, 0, 0, 0});
    const auto before = snap(*g);
    rejects([&] { g->submit_qword({16, 16, 0, 0x8000}); });
    check(g->state() == *before && g->state().vertex_pending,
          "ADC1 consumed vertex or drew instead of rejecting XYZ3");
    auto h = std::make_unique<Hardware>();
    auto state = snap(*h);
    state->graphics = *before;
    state->gif_owner = 3;
    h->restore(*state);
    check(h->write_quadword(0x10006000, {16, 16, 0, 0x8000}), "ADC fixture enqueue");
    const auto queued = snap(*h);
    h->advance(1, {});
    const auto failed = snap(*h);
    check(h->stop() && failed->graphics == queued->graphics &&
          failed->gif_fifo == queued->gif_fifo && failed->gif_owner == queued->gif_owner,
          "queued ADC1 mutated graphics or dequeued data");
    g->submit_qword({16, 16, 0, 0});
    const auto idle = snap(*g);
    for (unsigned descriptor : {2U, 11U, 13U}) {
        rejects([&] { tag(*g, 1, 2, (descriptor << 4) | 15, 0, true, 0); });
        check(g->state() == *idle, "unsupported later descriptor applied PRE first");
    }
    tag(*g, 1, 1, 15, 0, false, 0x7ff);
    check(g->state().primitive_ready, "PRE0 applied hostile primitive");
    g->submit_qword({~0U, ~0U, ~0U, ~0U});
    const auto empty = snap(*g);
    g->submit_qword({0xffff8000, 0xffffffff, 0xbbbbbbbb, 0xbbbbbbbb});
    check(g->state() == *empty, "NLOOP0 applied hostile PRE/descriptors");
}

void path_equivalence() {
    constexpr Words packet[] {
        {0x8001, 0x10000000, 14, 0}, {0x10000, 0, 0x4c, 0},
        {0x8001, 0x40000000, 0x5510, 0}, {6, ~0U, ~0U, ~0U},
        {0x11, 0x22, 0x33, 0x44}, {0, 0, 0, 0}, {32, 16, 0, 0}
    };
    for (unsigned path : {2U, 3U}) {
        auto m = std::make_unique<Memory>();
        if (path == 2) m->write_quadword(0x10005000, {0, 0x5000000700000000ULL});
        for (const auto& w : packet) {
            m->write_quadword(path == 2 ? 0x10005000 : 0x10006000,
                {w[0] | (std::uint64_t{w[1]} << 32), w[2] | (std::uint64_t{w[3]} << 32)});
        }
        m->advance(10);
        check(!m->hardware().stop() && m->hardware().graphics().pixels[0] == 0x44332211 &&
              m->hardware().graphics().pixels[1] == 0x44332211 &&
              m->hardware().graphics().pixels[2] == 0 &&
              m->hardware().graphics().rgbaq_q == 0x3f800000,
              "native PACKED descriptors differ across PATH2/PATH3");
    }
}
}
int main() {
    try {
        literal_extraction_and_q();
        wrap_nop_and_bounds();
        pre_and_adc_atomicity();
        path_equivalence();
        std::cout << "GIF PACKED tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
