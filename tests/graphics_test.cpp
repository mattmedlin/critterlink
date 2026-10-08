#include "critterlink/graphics.hpp"
#include <iostream>
#include <stdexcept>
using critterlink::Graphics;
namespace {
void check(bool v, const char* msg) { if (!v) throw std::runtime_error(msg); }
void tag(Graphics& g, std::uint32_t count) { g.submit_qword({count | 0x8000U, 0x10000000U, 14, 0}); }
void reg(Graphics& g, std::uint32_t addr, std::uint64_t v) {
    g.submit_qword({static_cast<std::uint32_t>(v), static_cast<std::uint32_t>(v >> 32), addr, 0});
}
std::uint64_t xy(unsigned x, unsigned y) { return (std::uint64_t{y} << 20) | (std::uint64_t{x} << 4); }
template<class F> void rejected(F fn) {
    try { fn(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("missing rejection");
}
}
int main() {
    try {
        Graphics g;
        tag(g, 5);
        reg(g, 0, 6); reg(g, 0x4c, 0x10000); reg(g, 1, 0x80402010);
        reg(g, 5, xy(2, 3));
        auto partial = g.state();
        reg(g, 5, xy(5, 7));
        const auto expected = g.state();
        for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x)
            check(expected.pixels[y * 64 + x] == ((x >= 2 && x < 5 && y >= 3 && y < 7) ? 0x80402010U : 0U), "sprite coverage");
        g.restore(partial); reg(g, 5, xy(5, 7)); check(g.state() == expected, "midpacket replay");
        tag(g, 5);
        reg(g, 0x4c, 0xff00000000010000ULL);
        reg(g, 0x40, 3ULL | (4ULL << 16) | (4ULL << 32) | (5ULL << 48));
        reg(g, 1, 0xffffffff); reg(g, 5, xy(0, 0)); reg(g, 5, xy(64, 64));
        check(g.state().pixels[4 * 64 + 3] == 0x80ffffff, "frame mask");
        check(g.state().pixels[3 * 64 + 3] == 0x80402010, "scissor");
        tag(g, 4); reg(g, 0x18, 16ULL | (16ULL << 32));
        reg(g, 0x40, (63ULL << 16) | (63ULL << 48));
        reg(g, 5, xy(0, 0)); reg(g, 5, xy(2, 2));
        check(g.state().pixels[0] == 0x00ffffff, "offset negative clipping");
        auto good = g.state();
        rejected([&] { g.submit_qword({1, 0x14000000, 2, 0}); });
        check(g.state() == good, "unsupported tag mutated state");
        rejected([&] { g.submit_qword({1, 0x10000000, 5, 0}); });
        tag(g, 1); good = g.state();
        rejected([&] { reg(g, 0, 0x16); });
        check(g.state() == good, "unsupported primitive consumed packet");
        rejected([&] { reg(g, 0x4c, 0x20000); });
        rejected([&] { reg(g, 5, 1); });
        rejected([&] { reg(g, 0x55, 0); });
        rejected([&] { g.submit_qword({0, 0, 0x100, 0}); });
        auto bad = g.state(); bad.remaining = 32768;
        rejected([&] { g.restore(bad); }); check(g.state() == good, "bad restore mutated state");
        reg(g, 0x7f, 0); check(g.state().remaining == 0, "payload accounting");
        tag(g, 0); check(g.state().remaining == 0, "zero loop");
        tag(g, 32767); check(g.state().remaining == 32767, "max loop");
        g.reset(); check(g.state() == critterlink::GraphicsState{}, "reset");
        // PRE embeds PRIM in the tag; subsequent XYZ2 writes must use it.
        g.submit_qword({0x8004, 0x10034000, 14, 0});
        reg(g, 0x4c, 0x10000); reg(g, 1, 0x11223344);
        reg(g, 5, xy(1, 1)); reg(g, 5, xy(0, 0));
        check(g.state().pixels[0] == 0x11223344 && g.state().pixels[1] == 0,
              "PRE primitive and reversed corners");
        std::cout << "graphics tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
