#include "critterlink/system.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace critterlink;
constexpr std::uint32_t mode = 0x10003010, status = 0x10003020, control = 0x10003000;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class T> auto snap(const T& object) {
    using S = std::remove_cvref_t<decltype(object.state())>;
    return std::unique_ptr<S>(new S(object.state()));
}
template<class F> void rejects(F fn) {
    try { fn(); }
    catch (const MemoryFault&) { return; }
    throw std::runtime_error("missing rejection");
}
void vif(Memory& m, std::uint32_t command) {
    m.write_quadword(0x10005000, {command, 0});
    m.advance(1);
}
void packet(Memory& m, std::uint32_t rgba, bool end = true) {
    m.write_quadword(0x10006000, {0x1000000000000001ULL | (end ? 0x8000ULL : 0), 14});
    m.write_quadword(0x10006000, {rgba, 1});
}

void access_and_independent_masks() {
    auto m = std::make_unique<Memory>();
    check((m->read(status, 4) & 3) == 0, "initial mask bits");
    m->write(0x90003010, 4, 1);
    check((m->read(status, 4) & 3) == 1, "M3R alias write/status");
    vif(*m, 0x06008000);
    check((m->read(status, 4) & 3) == 3, "independent M3P/M3R status");
    const auto both = snap(*m);
    rejects([&] { (void)m->read(mode, 4); });
    for (unsigned width : {1U, 2U, 8U}) rejects([&] { m->write(mode, width, 0); });
    for (unsigned value : {2U, 4U, 5U, 0x80000000U}) {
        rejects([&] { m->write(mode, 4, value); });
        check(*snap(*m) == *both, "unsupported MODE write mutated state");
    }
    for (bool clear_vif_first : {false, true}) {
        m->restore(*both);
        packet(*m, 0x12345678);
        if (clear_vif_first) vif(*m, 0x06000000);
        else m->write(0xb0003010, 4, 0);
        m->advance(3);
        check(m->hardware().graphics().rgba == 0 && (m->read(status, 4) & 0x40),
              "clearing one independent mask granted PATH3");
        if (clear_vif_first) m->write(mode, 4, 0);
        else vif(*m, 0x06000000);
        m->advance(2);
        check(m->hardware().graphics().rgba == 0x12345678 &&
              snap(m->hardware())->gif_fifo.count == 0, "second unmask lost queued packet");
    }
}

void masked_queue_and_path2() {
    auto m = std::make_unique<Memory>();
    m->write(mode, 4, 1);
    vif(*m, 0x06008000);
    packet(*m, 0x11111111);
    m->write_quadword(0x10005000, {0, 0x5000000200000000ULL});
    m->write_quadword(0x10005000, {0x1000000000008001ULL, 14});
    m->write_quadword(0x10005000, {0x22222222, 1});
    m->advance(4);
    check(m->hardware().graphics().rgba == 0x22222222 &&
          snap(m->hardware())->gif_fifo.count == 2 && (m->read(status, 4) & 0x43) == 0x43,
          "masks blocked PATH2 or dropped masked PATH3 bytes");
    const auto masked = snap(*m);
    vif(*m, 0x06000000);
    m->write(mode, 4, 0);
    m->advance(2);
    check(m->hardware().graphics().rgba == 0x11111111, "masked PATH3 packet output");
    const auto done = snap(*m);
    m->restore(*masked);
    vif(*m, 0x06000000);
    m->write(mode, 4, 0);
    m->advance(2);
    check(*snap(*m) == *done, "masked queue snapshot replay");
}

void active_packet_boundary() {
    auto m = std::make_unique<Memory>();
    packet(*m, 0x11111111, false);
    m->advance(2);
    m->write(mode, 4, 1);
    m->advance(3); // Owned packet has an empty-FIFO gap after an EOP0 tag.
    check(snap(m->hardware())->gif_owner == 3 && (m->read(status, 4) & 1),
          "mask interrupted EOP0 ownership during empty gap");
    packet(*m, 0x22222222);
    packet(*m, 0x33333333);
    const auto pending = snap(*m);
    m->advance(4);
    check(m->hardware().graphics().rgba == 0x22222222 &&
          snap(m->hardware())->gif_fifo.count == 2 && snap(m->hardware())->gif_owner == 0,
          "mask failed to drain current packet or blocked only at tag boundary");
    const auto stopped = snap(*m);
    m->restore(*pending);
    m->advance(1);
    m->advance(3);
    check(*snap(*m) == *stopped, "active-packet masked replay");
    m->write(mode, 4, 0);
    m->advance(2);
    check(m->hardware().graphics().rgba == 0x33333333, "following packet not preserved");
}

void empty_masked_bus_direction() {
    auto m = std::make_unique<Memory>();
    m->write(mode, 4, 1);
    m->write_quadword(0x10005000, {0, 0x5000000500000000ULL});
    m->write_quadword(0x10005000, {0x1000000000008004ULL, 14});
    m->write_quadword(0x10005000, {1ULL << 16, 0x50});
    m->write_quadword(0x10005000, {0, 0x51});
    m->write_quadword(0x10005000, {(1ULL << 32) | 4, 0x52});
    m->write_quadword(0x10005000, {1, 0x53});
    m->advance(7);
    m->write(0x10003c00, 4, 1U << 23);
    m->write(0x12001040, 8, 1);
    check((m->read(status, 4) & 0x1003) == 0x1001,
          "BUSDIR reset mask or masked-empty forward path could not reverse");
    m->advance(1);
    check(m->read_quadword(0x10005000) == std::array<std::uint64_t, 2>{0, 0},
          "M3R interfered with reverse data");
    m->write(0x10003c00, 4, 0);
    m->write(0x12001040, 8, 0);
    check((m->read(status, 4) & 0x1003) == 1, "forward restore changed M3R");
}

void reset_pause_and_flush() {
    auto m = std::make_unique<Memory>();
    m->write(mode, 4, 1);
    vif(*m, 0x06008000);
    packet(*m, 0x11111111);
    m->write_quadword(0x10005000, {0, 0x5000000100000000ULL});
    m->advance(1);
    m->write_quadword(0x10005000, {0x1000000000008001ULL, 14});
    const auto before = snap(m->hardware());
    m->write(control, 4, 1);
    const auto reset = snap(m->hardware());
    check((m->read(status, 4) & 3) == 2 && !reset->gif_path3_masked &&
          reset->path3_masked && reset->gif_fifo.count == 0 && reset->gif_owner == 0 &&
          reset->vif_input_fifo == before->vif_input_fifo &&
          reset->vif_transport == before->vif_transport &&
          reset->graphics.vram == before->graphics.vram &&
          reset->graphics.finish_event == before->graphics.finish_event,
          "GIF reset did not separate GIF-owned and VIF-owned mask/transport");

    auto paused = std::make_unique<Memory>();
    paused->write(mode, 4, 1);
    paused->write(control, 4, 8);
    packet(*paused, 0xabcdef);
    paused->write(mode, 4, 0);
    paused->advance(3);
    check(paused->hardware().graphics().rgba == 0 && (paused->read(status, 4) & 8),
          "unmask canceled independent PSE pause");
    paused->write(control, 4, 0);
    paused->advance(2);
    check(paused->hardware().graphics().rgba == 0xabcdef, "PSE resume lost masked packet");

    auto flush = std::make_unique<Memory>();
    flush->write(mode, 4, 1);
    packet(*flush, 0x55667788);
    vif(*flush, 0x13000000);
    flush->advance(4);
    check(snap(flush->hardware())->vif_transport.wait == VifWait::gif &&
          snap(flush->hardware())->vif_input_fifo.count == 1,
          "FLUSHA treated masked requested PATH3 as idle");
    flush->write(mode, 4, 0);
    flush->advance(3);
    check(flush->hardware().graphics().rgba == 0x55667788 &&
          snap(flush->hardware())->vif_input_fifo.count == 0,
          "CPU MODE clear failed to release original FLUSHA head");
}
}
int main() {
    try {
        access_and_independent_masks();
        masked_queue_and_path2();
        active_packet_boundary();
        empty_masked_bus_direction();
        reset_pause_and_flush();
        std::cout << "GIF MODE tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
