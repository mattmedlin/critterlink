#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>
namespace {
using namespace critterlink;
using Qword = std::array<std::uint64_t, 2>;
constexpr std::uint32_t chcr = 0x1000a000, madr = 0x1000a010, qwc = 0x1000a020;
constexpr std::uint32_t tadr = 0x1000a030, asr0 = 0x1000a040, asr1 = 0x1000a050;
constexpr std::uint32_t dctrl = 0x1000e000, dstat = 0x1000e010, gctrl = 0x10003000;
constexpr Qword payload {
    0x1234567887654321ULL, 0xfedcba9876543210ULL
};

void check(bool ok, const char* why) {
    if (!ok) { throw std::runtime_error(why); }
}

template<class T> auto snap(const T& o) {
    using S = std::remove_cvref_t<decltype(o.state())>;
    return std::unique_ptr<S>(new S(o.state()));
}

template<class T> auto copy(const T& s) {
    return std::unique_ptr<T>(new T(s));
}

template<class E = std::invalid_argument, class F> void rejects(F f) {
    try {
        f();
    }
    catch (const E&) {
        return;
    }
    throw std::runtime_error("expected rejection");
}

void tag(Memory& m, std::uint32_t at, unsigned id, unsigned count, std::uint32_t address = 0, bool irq = false) {
    m.write_quadword(at, {
        (std::uint64_t{address} << 32U) | (std::uint64_t{id} << 28U) |
        (irq ? 0x80000000ULL : 0ULL) | count, 0
    });
}

void start(Memory& m, std::uint32_t at = 0x2000, std::uint32_t control = 0x185) {
    m.write(gctrl, 4, 8);
    m.write(dctrl, 4, 1);
    m.write(tadr, 4, at);
    m.write(chcr, 4, control);
}

void captured(const HardwareState& h, unsigned n, Qword value) {
    const auto& w = h.gif_fifo.words[(h.gif_fifo.head+n)%16];
    check(w == std::array<std::uint32_t, 4> {
        static_cast<std::uint32_t>(value[0]), static_cast<std::uint32_t>(value[0]>>32U), static_cast<std::uint32_t>(value[1]), static_cast<std::uint32_t>(value[1]>>32U)
    }, "FIFO literal qword mismatch");
}

void tag_matrix() {
    for (unsigned id = 0; id<8; ++id) {
        auto mp = std::make_unique<Memory>();
        auto& m = *mp;
        const bool reference = id == 0 || id == 3 || id == 4;
        tag(m, 0x2000, id, 1, reference?0x3000:0x2100);
        m.write_quadword(reference?0x3000:0x2010, payload);
        tag(m, id == 2 || id == 5?0x2100:reference?0x2010:0x2020, 7, 0);
        start(m);
        m.advance(1);
        const auto mid_payload = id == 7 ? snap(m) : nullptr;
        auto fetched = snap(m.hardware());
        check(fetched->dma.qwords == 1 && fetched->gif_fifo.count == 0, "tag fetch must not transfer payload");
        m.advance(1);
        auto h = snap(m.hardware());
        check(h->gif_fifo.count == 1, "tag failed to enqueue");
        captured(*h, 0, payload);
        check((h->dma.chcr&0x70000000U) == id<<28U, "CHCR TAG ID mirror");
        if (id == 0 || id == 6 || id == 7) {
            check((h->dma.chcr & 0x100) == 0, "terminal tag did not complete");
        } else {
            m.advance(1);
            check((m.read(chcr, 4)&0x100) == 0, "following END did not complete");
        }
        check((m.read(dstat, 4)&4) != 0 && !m.hardware().stop(), "tag completion status");
        if (mid_payload) {
            const auto completed = snap(m);
            m.restore(*mid_payload);
            m.advance(1);
            check(*snap(m) == *completed, "mid-payload END replay");
        }
    }
}

void preload_and_nested() {
    // Fresh TAG0/QWC0 is the SDK launch recipe even after a terminal REFE.
    auto rp = std::make_unique<Memory>();
    auto& r = *rp;
    tag(r, 0x2000, 0, 0, 0x3000);
    start(r);
    r.advance(1);
    check(!(r.read(chcr, 4)&0x100), "empty REFE completion");
    r.write(chcr, 4, 0x185);
    r.advance(1);
    check(!r.hardware().stop() && !(r.read(chcr, 4)&0x100), "canonical restart after REFE rejected");
    auto mp = std::make_unique<Memory>();
    auto& m = *mp;
    tag(m, 0x2000, 7, 0);
    m.write_quadword(0x3000, payload);
    m.write(madr, 4, 0x3000);
    m.write(qwc, 4, 1);
    start(m, 0x2000, 0x10000105);
    m.advance(1);
    check(snap(m.hardware())->gif_fifo.count == 1 && m.read(tadr, 4) == 0x2000, "preloaded CNT packet");
    m.advance(1);
    check((m.read(chcr, 4)&0x100) == 0, "preloaded CNT continuation");
    auto np = std::make_unique<Memory>();
    auto& n = *np;
    tag(n, 0x2000, 5, 0, 0x2100);
    tag(n, 0x2010, 7, 0);
    tag(n, 0x2100, 5, 0, 0x2200);
    tag(n, 0x2110, 6, 0);
    tag(n, 0x2200, 6, 0);
    start(n);
    n.advance(1);
    check(n.read(asr0, 4) == 0x2010 && ((n.read(chcr, 4)>>4)&3) == 1, "CALL push1");
    n.advance(1);
    check(n.read(asr1, 4) == 0x2110 && ((n.read(chcr, 4)>>4)&3) == 2, "CALL push2");
    const auto nested = snap(n);
    n.advance(1);
    check(n.read(tadr, 4) == 0x2110 && ((n.read(chcr, 4)>>4)&3) == 1, "RET pop2");
    n.advance(1);
    check(n.read(tadr, 4) == 0x2010 && ((n.read(chcr, 4)>>4)&3) == 0, "RET pop1");
    n.advance(1);
    check(n.read(asr0, 4) == 0x2010 && n.read(asr1, 4) == 0x2110 && (n.read(chcr, 4)&0x100) == 0,
          "nested completion retained stack slots");
    const auto completed_nested = snap(n);
    n.restore(*nested);
    n.advance(1); n.advance(2);
    check(*snap(n) == *completed_nested, "nested CALL-stack snapshot replay");
    auto op = std::make_unique<Memory>();
    auto& o = *op;
    tag(o, 0x2000, 5, 0, 0x2100);
    tag(o, 0x2100, 5, 0, 0x2200);
    tag(o, 0x2200, 5, 1, 0x2300, true);
    start(o);
    o.advance(2);
    const auto before = snap(o.hardware());
    o.advance(1);
    const auto after = snap(o.hardware());
    check(!(after->dma.chcr&0x100) &&
          after->dma.qwords == 0 &&
          after->dma.address == before->dma.address &&
          after->dma.tag_address == 0x2200 &&
          after->dma.asr == before->dma.asr &&
          after->gif_fifo.count == 0 &&
          (after->dma.status&0x8004) == 4 &&
          !o.hardware().stop(),
          "CALL overflow must terminate without payload/bus error");
}

void tie_resume_and_loop() {
    auto mp = std::make_unique<Memory>();
    auto& m = *mp;
    tag(m, 0x2000, 5, 1, 0x2100, true);
    m.write_quadword(0x2010, payload);
    tag(m, 0x2020, 7, 0);
    tag(m, 0x2100, 6, 1, 0, true);
    m.write_quadword(0x2110, {
        7, 8
    });
    start(m);
    m.advance(2);
    check(!(m.read(chcr, 4)&0x100) && m.read(tadr, 4) == 0x2100, "TIE CALL boundary");
    const auto paused = snap(m);
    m.write(chcr, 4, m.read(chcr, 4)|0x100);
    m.advance(2);
    check(!(m.read(chcr, 4)&0x100) && m.read(tadr, 4) == 0x2020, "TIE RET boundary");
    m.write(chcr, 4, m.read(chcr, 4)|0x100);
    m.advance(1);
    const auto done = snap(m);
    check(done->hardware.gif_fifo.count == 2, "TIE replay duplicated payload");
    captured(done->hardware, 0, payload);
    captured(done->hardware, 1, {
        7, 8
    });
    m.restore(*paused);
    m.write(chcr, 4, m.read(chcr, 4)|0x100);
    m.advance(1);
    m.advance(1);
    m.write(chcr, 4, m.read(chcr, 4)|0x100);
    m.advance(1);
    check(*snap(m) == *done, "TIE checkpoint replay");
    auto lp = std::make_unique<Memory>();
    auto& l = *lp;
    tag(l, 0x2000, 2, 0, 0x2000);
    start(l);
    l.advance(23);
    check(l.hardware().now() == 23 && l.read(tadr, 4) == 0x2000 && (l.read(chcr, 4)&0x100) && !l.hardware().stop(),
          "zero-QWC self loop not bounded per tick");
}

void pauses_and_failures() {
    auto mp = std::make_unique<Memory>();
    auto& m = *mp;
    tag(m, 0x2000, 7, 1);
    m.write_quadword(0x2010, payload);
    start(m);
    for (unsigned n = 0; n<16; ++n)m.write_quadword(0x10006000, {
        0x1000000000008000ULL, 14
    });
    m.advance(1);
    const auto full = snap(m.hardware());
    check(full->dma.phase == GifChainPhase::payload && full->dma.qwords == 1 && full->gif_fifo.count == 16,
          "full FIFO blocked tag fetch instead of payload");
    m.advance(3);
    check(snap(m.hardware())->dma == full->dma && snap(m.hardware())->gif_fifo == full->gif_fifo,
          "FIFO stall advanced chain");
    m.write(dctrl, 4, 0);
    const auto disabled = snap(m.hardware());
    m.advance(3);
    check(snap(m.hardware())->dma == disabled->dma, "DMAE pause changed decoder");
    m.write(dctrl, 4, 1);
    m.write(gctrl, 4, 0);
    m.advance(2);
    check(!(m.read(chcr, 4)&0x100), "FIFO resumed chain");
    for (bool bad_payload: {
        false, true
    }) {
        auto ep = std::make_unique<Memory>();
        auto& e = *ep;
        if (bad_payload)tag(e, 0x2000, 0, 1, 0x02000000);
        start(e, bad_payload?0x2000:0x02000000);
        e.advance(bad_payload?2:1);
        const auto s = snap(e.hardware());
        check(e.hardware().stop().has_value() &&
              !(s->dma.chcr&0x100) &&
              (s->dma.status&0x8004) == 0x8004 &&
              s->dma.phase == GifChainPhase::idle &&
              !s->dma.packet_end &&
              !s->dma.packet_irq &&
              s->dma.next_tag == 0,
              "chain RAM bus error cleanup");
    } for (std::uint64_t raw: {
        0x74000000ULL, 0x8000000070000000ULL
    }) {
        auto up = std::make_unique<Memory>();
        auto& u = *up;
        u.write_quadword(0x2000, {
            raw, 0
        });
        start(u);
        const auto prior = snap(u.hardware());
        u.advance(1);
        const auto after = snap(u.hardware());
        check(u.hardware().stop().has_value() && after->dma == prior->dma && after->gif_fifo == prior->gif_fifo,
              "unsupported tag partly committed");
    }
}

void snapshot_validation() {
    auto mp = std::make_unique<Memory>();
    auto& m = *mp;
    tag(m, 0x2000, 7, 1);
    m.write_quadword(0x2010, payload);
    start(m);
    m.advance(1);
    const auto good = snap(m.hardware());
    for (unsigned kind = 0; kind<6; ++kind) {
        auto bad = copy(*good);
        switch (kind) {
        case 0: bad->dma.phase = GifChainPhase::idle; break;
        case 1: bad->dma.qwords = 0; break;
        case 2: bad->dma.next_tag = 3; break;
        case 3: bad->dma.asr[0] = 1; break;
        case 4: bad->dma.chcr |= 0x30; break;
        default: bad->dma.tag_address = 1; break;
        }
        auto validator = std::make_unique<Hardware>();
        validator->restore(*good);
        rejects([&] {
            validator->restore(*bad);
        });
        check(*snap(*validator) == *good, "invalid Hardware restore mutated state");
        check(*snap(m.hardware()) == *good, "invalid chain snapshot not atomic");
    } for (auto address: {
        madr, qwc, tadr, asr0, asr1
    }) {
        rejects<MemoryFault>([&] {
            m.write(address, 4, 0);
        });
        check(*snap(m.hardware()) == *good, "active register write mutated state");
    }
}

void continuation_contract() {
    // Terminal tags win over IRQ/TIE; only a fresh launch may follow completion.
    for (unsigned id: {
        0U, 6U, 7U
    }) {
        auto mp = std::make_unique<Memory>();
        auto& m = *mp;
        tag(m, 0x2000, id, 1, 0x3000, true);
        m.write_quadword(id == 0?0x3000:0x2010, payload);
        start(m);
        m.advance(2);
        const auto done = snap(m.hardware());
        check(done->dma.phase == GifChainPhase::idle &&
              !(done->dma.chcr&0x100) &&
              done->gif_fifo.count == 1 &&
              !done->dma.packet_irq &&
              !done->dma.packet_end &&
              done->dma.next_tag == 0,
              "terminal IRQ tag retained resumable state");
        rejects<MemoryFault>([&] {
            m.write(chcr, 4, done->dma.chcr|0x100);
        });
        check(*snap(m.hardware()) == *done, "terminal restart rejection mutated state");
    }
    // An explicit stopped register edit abandons the saved TIE continuation.
    for (auto address: {
        tadr, qwc, asr0, asr1
    }) {
        auto mp = std::make_unique<Memory>();
        auto& m = *mp;
        tag(m, 0x2000, 1, 0, 0, true);
        tag(m, 0x2010, 7, 0);
        start(m);
        m.advance(1);
        const auto oldchcr = m.read(chcr, 4);
        check(snap(m.hardware())->dma.phase == GifChainPhase::tag && !(oldchcr&0x100), "CNT IRQ did not pause at next tag");
        m.write(address, 4, m.read(address, 4));
        const auto edited = snap(m.hardware());
        check(edited->dma.phase == GifChainPhase::idle, "stopped programming preserved continuation");
        rejects<MemoryFault>([&] {
            m.write(chcr, 4, oldchcr|0x100);
        });
        check(*snap(m.hardware()) == *edited, "abandoned IRQ TAG restart partly mutated");
        m.write(chcr, 4, 0x185);
        m.advance(1);
        check(!m.hardware().stop() && !(m.read(chcr, 4)&0x100), "fresh canonical launch after edit failed");
    } auto cp = std::make_unique<Memory>();
    auto& c = *cp;
    tag(c, 0x2000, 7, 0);
    start(c, 0x2000, 0x10000105);
    c.advance(1);
    check(!(c.read(chcr, 4)&0x100) && !c.hardware().stop(), "CNT/QWC0 failed direct tag fetch");
    // Unsupported second fetch must preserve the previous committed CALL stack/TAG.
    auto up = std::make_unique<Memory>();
    auto& u = *up;
    tag(u, 0x2000, 5, 0, 0x2100);
    u.write_quadword(0x2100, {
        0x74000000, 0
    });
    start(u);
    u.advance(1);
    const auto valid = snap(u.hardware());
    u.advance(1);
    const auto failed = snap(u.hardware());
    check(u.hardware().stop() && failed->dma == valid->dma && failed->gif_fifo == valid->gif_fifo,
          "unsupported later fetch clobbered committed decoder state");
    for (bool payload_error: {
        false, true
    }) {
        auto ep = std::make_unique<Memory>();
        auto& e = *ep;
        if (payload_error)tag(e, 0x2000, 0, 1, 0x02000000);
        start(e, payload_error?0x2000:0x02000000);
        e.advance(payload_error?2:1);
        const auto stopped = snap(e.hardware());
        check(stopped->dma.phase == GifChainPhase::idle && stopped->dma.next_tag == 0 && !stopped->dma.packet_end && !stopped->dma.packet_irq,
              "bus error retained chain metadata");
        const auto saved = snap(e);
        e.restore(*saved);
        check(*snap(e.hardware()) == *stopped, "normalized bus error snapshot cannot roundtrip");
    }
}

void guest_render_replay() {
    auto sp = std::make_unique<System>();
    auto& s = *sp;
    auto& m = s.memory();
    constexpr std::array<Qword, 6> sprite {
        {
            {
                0x1000000000008005ULL, 14
            }, {
                6, 0
            }, {
                0x10000, 0x4c
            }, {
                0x80402010, 1
            }, {
                0x00300020, 5
            }, {
                0x00700050, 5
            }
        }
    };
    tag(m, 0x2000, 7, 6);
    for (unsigned n = 0; n<6; ++n)m.write_quadword(0x2010+n*16, sprite[n]);
    // Original guest configures DMAE, channel mask, TADR and SDK chain launch.
    constexpr std::uint32_t guest[] {
        0x3c081001, 0x34090001, 0xad09e000, 0x3c090004, 0xad09e010, 0x34092000, 0xad09a030, 0x34090185, 0xad09a000, 0x1000ffff, 0
    };
    for (unsigned i = 0; i<sizeof(guest)/4; ++i)m.write(i*4, 4, guest[i]);
    constexpr std::uint32_t handler[] {
        0x3c081001, 0x8d09e010, 0xac091000, 0x34090004, 0xad09e010, 0x26100001, 0x42000018
    };
    for (unsigned i = 0; i<sizeof(handler)/4; ++i)m.write(0x200+i*4, 4, handler[i]);
    auto cpu = snap(s.cpu());
    cpu->cop0.status = 0x10801;
    s.cpu().restore(*cpu);
    check(s.run(9).budget_exhausted, "guest launch");
    const auto pending = snap(s);
    std::vector<InstructionTrace>a, b;
    check(s.run(30, &a).budget_exhausted &&
          !m.hardware().stop() &&
          s.cpu().state().gpr[16].low == 1 &&
          (m.read(0x1000, 4)&4) != 0 &&
          !m.hardware().int1(),
          "chain render interrupt handler");
    for (unsigned y = 0; y < 64; ++y) {
        for (unsigned x = 0; x < 64; ++x) {
            const auto expected_pixel = x >= 2 && x < 5 && y >= 3 && y < 7 ? 0x80402010U : 0U;
            check(m.hardware().graphics().pixels[y * 64 + x] == expected_pixel,
                  "chain-render literal pixels");
        }
    }
    const auto expected = snap(s);
    s.restore(*pending);
    check(s.run(11, &b).budget_exhausted && s.run(19, &b).budget_exhausted && *snap(s) == *expected && a == b,
          "original chain guest replay");
}
}

int main() {
    try {
        tag_matrix();
        preload_and_nested();
        tie_resume_and_loop();
        pauses_and_failures();
        snapshot_validation();
        continuation_contract();
        guest_render_replay();
        std::cout<<"GIF source chain integration tests passed\n";
    }
    catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
