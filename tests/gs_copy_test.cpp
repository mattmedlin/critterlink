#include "critterlink/system.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace critterlink;
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
void packed(Graphics& g, unsigned count) {
    g.submit_qword({count | 0x8000U, 0x10000000, 14, 0});
}
void reg(Graphics& g, unsigned address, std::uint64_t value) {
    g.submit_qword({static_cast<std::uint32_t>(value),
                    static_cast<std::uint32_t>(value >> 32), address, 0});
}
void write(Graphics& g, unsigned address, std::uint64_t value) {
    packed(g, 1);
    reg(g, address, value);
}
void seed(Graphics& g) {
    packed(g, 4);
    reg(g, 0x50, 1ULL << 48);
    reg(g, 0x51, 0);
    reg(g, 0x52, (2ULL << 32) | 2);
    reg(g, 0x53, 0);
    write(g, 0x54, 0x0000002200000011ULL);
    write(g, 0x54, 0x0000004400000033ULL);
}
void configure(Graphics& g, unsigned direction = 0, unsigned dbp = 1) {
    packed(g, 4);
    reg(g, 0x50, (1ULL << 16) | (std::uint64_t{dbp} << 32) | (1ULL << 48));
    reg(g, 0x51, std::uint64_t{direction} << 59);
    reg(g, 0x52, (2ULL << 32) | 2);
    reg(g, 0x53, 2);
}
void tick(Graphics& g, unsigned count = 1) {
    for (unsigned n = 0; n < count; ++n) g.tick();
}

void directions_and_odd_replay() {
    // Sony GS p74: upper-left origins, DIR selects starting corner, not image flip.
    constexpr std::array<std::array<unsigned, 4>, 4> order {{
        {0, 1, 2, 3}, {2, 3, 0, 1}, {1, 0, 3, 2}, {3, 2, 1, 0}
    }};
    constexpr std::array<std::uint32_t, 4> values {0x11, 0x22, 0x33, 0x44};
    for (unsigned direction = 0; direction < 4; ++direction) {
        auto g = std::make_unique<Graphics>();
        seed(*g);
        configure(*g, direction);
        check(g->state().vram[64] == 0, "copy started synchronously in TRXDIR");
        tick(*g);
        const auto odd = snap(*g);
        check(odd->transfer.cursor == 1, "one logical tick did not copy one pixel");
        std::array<bool, 4> written{};
        written[order[direction][0]] = true;
        for (unsigned step = 1; step <= 4; ++step) {
            if (step > 1) {
                tick(*g);
                written[order[direction][step - 1]] = true;
            }
            for (unsigned n = 0; n < 4; ++n) {
                check(g->state().vram[64 + n] == (written[n] ? values[n] : 0),
                      "DIR first/intermediate raw destination order");
                check(g->state().vram[n] == values[n], "copy changed source");
            }
        }
        check(!g->state().transfer.active, "copy did not end at exact rectangle size");
        const auto done = snap(*g);
        g->restore(*odd);
        tick(*g, 1);
        tick(*g, 2);
        check(g->state() == *done, "odd copy cursor snapshot replay");
    }
}

void unread_source_changes_between_ticks() {
    auto g = std::make_unique<Graphics>();
    seed(*g);
    configure(*g);
    tick(*g);
    auto checkpoint = snap(*g);
    // Declared live-read policy, not a claim about GS internal buffering timing.
    // Keep the fixed framebuffer view consistent with the changed source word.
    checkpoint->vram[1] = 0x12345678;
    checkpoint->pixels[1] = 0x12345678;
    g->restore(*checkpoint);
    tick(*g, 3);
    check(g->state().vram[64] == 0x11 && g->state().vram[65] == 0x12345678 &&
          g->state().vram[66] == 0x33 && g->state().vram[67] == 0x44,
          "copy buffered unread source pixels instead of reading each tick");
}

void different_strides_and_page_carry() {
    auto g = std::make_unique<Graphics>();
    packed(*g, 4);
    reg(*g, 0x50, 2ULL << 48);
    reg(*g, 0x51, 0);
    reg(*g, 0x52, (34ULL << 32) | 128);
    reg(*g, 0x53, 0);
    g->submit_qword({0x8000U | (128 * 34 / 4), 0x08000000, 0, 0});
    for (unsigned n = 0; n < 128 * 34; n += 4) g->submit_qword({n + 1, n + 2, n + 3, n + 4});
    packed(*g, 4);
    reg(*g, 0x50, (2ULL << 16) | (129ULL << 32) | (3ULL << 48));
    reg(*g, 0x51, 0);
    reg(*g, 0x52, (34ULL << 32) | 128);
    reg(*g, 0x53, 2);
    tick(*g, 128 * 34);
    // BP129=33024 bytes; destination BW3 changes the next page-row stride.
    check(g->state().vram[33024 / 4] == 1 &&
          g->state().vram[(33024 + 252) / 4] == 904 &&
          g->state().vram[(33024 + 8188) / 4] == 4032 &&
          g->state().vram[(33024 + 8192) / 4] == 65 &&
          g->state().vram[(33024 + 24576) / 4] == 4097 &&
          g->state().vram[(33024 + 16384) / 4] == 0,
          "copy stride, block, page or nonaligned BP carry");
}

void alias_and_invalid_atomicity() {
    auto g = std::make_unique<Graphics>();
    seed(*g);
    configure(*g);
    tick(*g);
    // BP0,x8 and BP1,x0 both address physical block1 despite different descriptions.
    write(*g, 0x50, (1ULL << 16) | (1ULL << 32) | (1ULL << 48));
    write(*g, 0x51, 8);
    packed(*g, 1);
    const auto before = snap(*g);
    rejects([&] { reg(*g, 0x53, 2); });
    check(g->state() == *before, "physical alias rejection changed active copy");
    reg(*g, 0x7f, 0);
    for (unsigned format : {1U, 2U, 0x30U, 0x13U}) {
        write(*g, 0x50, (std::uint64_t{format} << 24) | (1ULL << 16) |
                       (2ULL << 32) | (1ULL << 48));
        packed(*g, 1);
        const auto saved = snap(*g);
        rejects([&] { reg(*g, 0x53, 2); });
        check(g->state() == *saved, "unsupported source format replaced active copy");
        reg(*g, 0x7f, 0);
    }
    for (unsigned width : {0U, 33U}) {
        write(*g, 0x50, (std::uint64_t{width} << 16) | (2ULL << 32) | (1ULL << 48));
        packed(*g, 1);
        const auto saved = snap(*g);
        rejects([&] { reg(*g, 0x53, 2); });
        check(g->state() == *saved, "invalid source stride replaced active copy");
        reg(*g, 0x7f, 0);
    }
    const auto valid = snap(*g);
    for (unsigned mutation = 0; mutation < 6; ++mutation) {
        auto bad = std::unique_ptr<GraphicsState>(new GraphicsState(*valid));
        if (mutation == 0) bad->transfer.cursor = 5;
        if (mutation == 1) bad->transfer.active = false;
        if (mutation == 2) bad->vram.pop_back();
        if (mutation == 3) bad->transfer.direction = 4;
        if (mutation == 4) bad->transfer.source_width = 0;
        if (mutation == 5) bad->transfer.source_base = bad->transfer.base;
        rejects([&] { g->restore(*bad); });
        check(g->state() == *valid, "malformed copy restore was not atomic");
    }
}

void latching_cancel_and_hwreg() {
    auto g = std::make_unique<Graphics>();
    seed(*g);
    configure(*g);
    tick(*g);
    const auto prior_vram = g->state().vram;
    write(*g, 0x54, 0xffffffffffffffffULL);
    g->submit_qword({0x8001, 0x08000000, 0, 0});
    g->submit_qword({99, 99, 99, 99});
    check(g->state().vram == prior_vram && g->state().transfer.cursor == 1,
          "HWREG or IMAGE became copy data");
    write(*g, 0x50, (1ULL << 16) | (2ULL << 32) | (1ULL << 48));
    tick(*g, 3);
    check(g->state().vram[67] == 0x44 && g->state().vram[128] == 0,
          "parameter writes changed latched copy destination");
    write(*g, 0x53, 2);
    tick(*g);
    check(g->state().vram[128] == 0x11 && g->state().transfer.cursor == 1,
          "restart did not latch current destination");
    write(*g, 0x53, 3);
    tick(*g, 4);
    check(g->state().vram[129] == 0 && !g->state().transfer.active,
          "cancel continued writing pixels");
    configure(*g, 0, 3);
    tick(*g);
    write(*g, 0x53, 0); // Valid upload replaces active copy under the declared policy.
    write(*g, 0x54, 0x000000bb000000aaULL);
    tick(*g, 4);
    check(g->state().vram[192] == 0xaa && g->state().vram[193] == 0xbb &&
          g->state().transfer.cursor == 2, "upload replacement continued copy execution");
}

void hardware_tick_policy() {
    auto g = std::make_unique<Graphics>();
    seed(*g);
    configure(*g);
    write(*g, 0x53, 3);
    auto h = std::make_unique<Hardware>();
    auto state = snap(*h);
    state->graphics = g->state();
    h->restore(*state);
    check(h->write_quadword(0x10006000, {0x8001, 0x10000000, 14, 0}) &&
          h->write_quadword(0x10006000, {2, 0, 0x53, 0}), "copy command enqueue");
    h->advance(2, {});
    check(h->graphics().transfer.active && h->graphics().transfer.cursor == 0,
          "copy ran in same logical tick as TRXDIR consumption");
    h->write(0x10003000, 8);
    h->write(0x1000e000, 0);
    h->advance(3, {});
    check(h->graphics().transfer.cursor == 3, "GIF/DMA pause incorrectly paused GS copy");
    const auto odd = snap(*h);
    h->advance(1, {});
    const auto done = snap(*h);
    check(!h->graphics().transfer.active && !h->int0() && !h->int1() &&
          h->read(0x1000e010) == 0, "local copy invented completion interrupt");
    h->restore(*odd);
    h->advance(1, {});
    check(*snap(*h) == *done, "Hardware copy replay");
}

void guest_command_and_replay() {
    auto s = std::make_unique<System>();
    auto g = std::make_unique<Graphics>();
    seed(*g);
    configure(*g);
    write(*g, 0x53, 3);
    auto initial = snap(s->memory());
    initial->hardware.graphics = g->state();
    s->memory().restore(*initial);
    auto& m = s->memory();
    m.write_quadword(0x3000, {0x1000000000008001ULL, 14});
    m.write_quadword(0x3010, {2, 0x53});
    constexpr std::uint32_t guest[] {
        0x3c081000, 0x35086000, 0x34093000, 0x792a0000, 0x7d0a0000,
        0x792a0010, 0x7d0a0000, 0x1000ffff, 0
    };
    for (unsigned n = 0; n < std::size(guest); ++n) m.write(n * 4, 4, guest[n]);
    check(s->run(8).budget_exhausted, "copy guest launch");
    const auto partial = snap(*s);
    check(m.hardware().graphics().transfer.cursor == 1, "guest logical copy progress");
    std::vector<InstructionTrace> a, b;
    check(s->run(8, &a).budget_exhausted && !m.hardware().stop() &&
          m.hardware().graphics().vram[64] == 0x11 &&
          m.hardware().graphics().vram[67] == 0x44 &&
          !m.hardware().int0() && !m.hardware().int1(), "guest-commanded local copy");
    const auto done = snap(*s);
    s->restore(*partial);
    check(s->run(3, &b).budget_exhausted && s->run(5, &b).budget_exhausted &&
          *snap(*s) == *done && a == b, "guest copy full state and trace replay");
}
}
int main() {
    try {
        directions_and_odd_replay();
        unread_source_changes_between_ticks();
        different_strides_and_page_carry();
        alias_and_invalid_atomicity();
        latching_cancel_and_hwreg();
        hardware_tick_policy();
        guest_command_and_replay();
        std::cout << "GS local copy tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
