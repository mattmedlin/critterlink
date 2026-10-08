#include "critterlink/system.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

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
void word(VectorUnit& v, std::uint32_t w) { check(v.submit_word(w), "unexpected VIF wait"); }

void six_literal_formats() {
    for (bool usn : {false, true}) {
        const unsigned u = usn ? 0x4000 : 0;
        auto v = std::make_unique<VectorUnit>();
        word(*v, 0x60010000 | u);
        word(*v, 0x81234567);
        check(v->state().data[0] == std::array<std::uint32_t, 4>{0x81234567, 0x81234567, 0x81234567, 0x81234567},
              "S32 raw broadcast or USN effect");
        word(*v, 0x61030001 | u);
        word(*v, 0x7fff8000);
        word(*v, 0xaabbffff); // Odd scalar tail ignores upper16.
        check(v->state().data[1][0] == (usn ? 0x8000U : 0xffff8000U) &&
              v->state().data[2][3] == 0x7fff &&
              v->state().data[3][1] == (usn ? 0xffffU : 0xffffffffU), "S16 order/sign/padding");
        word(*v, 0x62030004 | u);
        word(*v, 0xaa807fff);
        check(v->state().data[4][0] == (usn ? 255U : 0xffffffffU) &&
              v->state().data[5][2] == 127 &&
              v->state().data[6][3] == (usn ? 128U : 0xffffff80U), "S8 literal broadcast/tail");
        word(*v, 0); // Tail padding ends at word boundary, next word is a command.
        word(*v, 0x6c010007 | u);
        for (unsigned w : {0x80000000U, 0x3f800000U, 0xffffffffU, 0x50000000U}) word(*v, w);
        check(v->state().data[7] == std::array<std::uint32_t, 4>{0x80000000, 0x3f800000, 0xffffffff, 0x50000000},
              "V432 command-shaped data or rawbits");
        word(*v, 0x6d010008 | u);
        word(*v, 0x7fff8000);
        word(*v, 0xffff0001);
        check(v->state().data[8] == std::array<std::uint32_t, 4>{usn ? 0x8000U : 0xffff8000U,
              0x7fff, 1, usn ? 0xffffU : 0xffffffffU}, "V416 literal expansion");
        word(*v, 0x6e010009 | u);
        word(*v, 0xff017f80);
        check(v->state().data[9] == std::array<std::uint32_t, 4>{usn ? 128U : 0xffffff80U,
              127, 1, usn ? 255U : 0xffffffffU}, "V48 literal expansion");
    }
}

void cycles_bounds_and_rejections() {
    auto v = std::make_unique<VectorUnit>();
    auto initial = snap(*v);
    for (auto& data : initial->data) data.fill(0xdeadbeef);
    v->restore(*initial);
    word(*v, 0x01ff0204); // NUM ignored, WL2/CL4.
    word(*v, 0x6005000a);
    for (unsigned n = 1; n <= 5; ++n) word(*v, n);
    constexpr unsigned addresses[] {10, 11, 14, 15, 18};
    for (unsigned n = 0; n < 5; ++n) {
        check(v->state().data[addresses[n]] == std::array<std::uint32_t, 4>{n + 1, n + 1, n + 1, n + 1},
              "STCYCL skip destination");
    }
    for (unsigned n : {9U, 12U, 13U, 16U, 17U, 19U}) {
        check(v->state().data[n][0] == 0xdeadbeef && v->state().data[n][3] == 0xdeadbeef,
              "skipped sentinel overwritten");
    }
    word(*v, 0x600503f7); // 1015,1016,1019,1020,1023.
    for (unsigned n = 0; n < 5; ++n) word(*v, n);
    check(v->state().data[1023][0] == 4, "last legal skipped destination");
    const auto before = snap(*v);
    for (unsigned invalid : {0x600503f8U, 0x01000001U, 0x01000100U, 0x01000201U,
            0x70010000U, 0x6c018000U, 0x6c010400U, 0x64010000U, 0x68010000U, 0x6f010000U}) {
        rejects([&] { (void)v->submit_word(invalid); });
        check(v->state() == *before, "invalid format/cycle/range changed VU state");
    }
    word(*v, 0x01000303);
    word(*v, 0x62000300); // NUM0=256, final address1023, 64 inputwords.
    for (unsigned n = 0; n < 64; ++n) word(*v, 0x01010101);
    check(v->state().data[1023][3] == 1 && v->state().payload == 0, "NUM0 scalar count");
    word(*v, 0x6c000000);
    check(v->state().payload_remaining == 1024, "maximum V432 payload word count");
}

void component_replay_and_invalid_state() {
    for (unsigned opcode : {0x6cU, 0x6dU}) {
        for (unsigned accepted = 1; accepted < (opcode == 0x6c ? 4U : 2U); ++accepted) {
            auto v = std::make_unique<VectorUnit>();
            word(*v, (opcode << 24) | 0x10000);
            for (unsigned n = 0; n < accepted; ++n) word(*v, 0x22221111 + n);
            const auto partial = snap(*v);
            for (unsigned n = accepted; n < (opcode == 0x6c ? 4U : 2U); ++n) word(*v, 0x44443333 + n);
            const auto done = snap(*v);
            v->restore(*partial);
            for (unsigned n = accepted; n < (opcode == 0x6c ? 4U : 2U); ++n) word(*v, 0x44443333 + n);
            check(v->state() == *done, "partial component replay");
            for (unsigned mutation = 0; mutation < 4; ++mutation) {
                auto bad = std::unique_ptr<VectorState>(new VectorState(*partial));
                if (mutation == 0) ++bad->payload_remaining;
                if (mutation == 1) bad->cycle_wl = 0;
                if (mutation == 2) bad->unpack_completed = bad->unpack_total;
                if (mutation == 3) bad->payload_lane = opcode == 0x6d ? 1 : 4;
                rejects([&] { v->restore(*bad); });
                check(v->state() == *done, "malformed UNPACK progress replaced VU state");
            }
        }
    }
}

void cpu_dma_tte_streams() {
    for (unsigned source = 0; source < 3; ++source) {
        auto m = std::make_unique<Memory>();
        // First word of V416 payload is at the final physical lane/upper TTE word.
        if (source == 0) {
            m->write_quadword(0x10005000, {0, 0x7fff80006d010000ULL});
        } else if (source == 1) {
            m->write_quadword(0x3000, {0, 0x7fff80006d010000ULL});
            m->write(0x1000e000, 4, 1);
            m->write(0x10009010, 4, 0x3000);
            m->write(0x10009020, 4, 1);
            m->write(0x10009000, 4, 0x181);
        } else {
            m->write_quadword(0x3000, {0x70000000, 0x7fff80006d010000ULL});
            m->write(0x1000e000, 4, 1);
            m->write(0x10009030, 4, 0x3000);
            m->write(0x10009000, 4, 0x1c5);
        }
        m->advance(source == 2 ? 2 : 1);
        check(m->hardware().vector().payload_lane == 2 && m->hardware().vector().payload_remaining == 1,
              "cross-source partial V416 checkpoint");
        const auto partial = snap(*m);
        m->write_quadword(0x10005000, {0x62030004ffff0001ULL, 0x06008000aa807fffULL});
        m->advance(1);
        check(!m->hardware().stop() && m->hardware().vector().data[0] ==
              std::array<std::uint32_t, 4>{0xffff8000, 0x7fff, 1, 0xffffffff} &&
              m->hardware().vector().data[6][3] == 0xffffff80 && snap(m->hardware())->path3_masked,
              "compact tail consumed following same-qword command");
        const auto done = snap(*m);
        m->restore(*partial);
        m->write_quadword(0x10005000, {0x62030004ffff0001ULL, 0x06008000aa807fffULL});
        m->advance(1);
        check(*snap(*m) == *done, "mixed CPU/DMA/TTE UNPACK replay");
    }
}

void vu_wait() {
    auto m = std::make_unique<Memory>();
    auto state = snap(*m);
    state->hardware.vector.micro[0] = {0x10000000, 0x400002ff};
    state->hardware.vector.micro[1] = {0x10000000, 0x000002ff};
    state->hardware.vector.running = true;
    m->restore(*state);
    m->write_quadword(0x10005000, {0xff017f806e010000ULL, 0});
    m->advance(1);
    check(m->hardware().vector().payload == 0 && snap(m->hardware())->vif_input_fifo.count == 1,
          "UNPACK modified payload while VU running");
    const auto waiting = snap(*m);
    m->advance(1);
    check(m->hardware().vector().data[0] ==
          std::array<std::uint32_t, 4>{0xffffff80, 127, 1, 0xffffffff}, "UNPACK did not resume after VU end");
    const auto done = snap(*m);
    m->restore(*waiting);
    m->advance(1);
    check(*snap(*m) == *done, "VU-wait compact payload replay");
}
}
int main() {
    try {
        six_literal_formats();
        cycles_bounds_and_rejections();
        component_replay_and_invalid_state();
        cpu_dma_tte_streams();
        vu_wait();
        std::cout << "VIF UNPACK tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
