#include "critterlink/spu.hpp"
#include <iostream>
#include <stdexcept>
using namespace critterlink;
namespace {
void check(bool v, const char*m) {
    if (!v) throw std::runtime_error(m);
}
template<class F>void rejects(F f) {
    try {
        f();
    }
    catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("expected rejection");
}

void wr(Spu&s, unsigned o, unsigned v) {
    s.write16(0x1f900000+o, static_cast<std::uint16_t>(v));
}

void setup(Spu&s) {
    wr(s, 0, 0x3fff);
    wr(s, 2, 0x3fff);
    wr(s, 4, 0x1000);
    wr(s, 6, 15);
    wr(s, 8, 0x1fc0);
    wr(s, 0x188, 1);
    wr(s, 0x190, 1);
    wr(s, 0x198, 0xff0);
    wr(s, 0x760, 0x3fff);
    wr(s, 0x762, 0x3fff);
    wr(s, 0x19a, 0xc000);
}

void upload(Spu&s, unsigned header, unsigned flags) {
    wr(s, 0x1ac, header|(flags<<8));
    for (unsigned i = 0; i<7; ++i)wr(s, 0x1ac, 0x7777);
    wr(s, 0x19a, 0x8010);
    for (unsigned i = 0; i<8; ++i)s.tick();
}

void pcm() {
    Spu s;
    upload(s, 0x1c, 3);
    setup(s);
    wr(s, 0x1a0, 1);
    constexpr int expected[] {
        2, 11, 18, 24, 29, 34, 39, 43, 47, 51, 55, 58, 61, 64, 67, 70, 73, 75, 77, 79, 81, 83, 85, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103
    };
    for (auto n: expected) {
        s.tick();
        check(s.state().last_sample[0] == n && s.state().last_sample[1] == n, "PCM oracle");
    }
    check(s.state().endx == 1, "loop ENDX");
    wr(s, 0x1a4, 1);
    s.tick();
    check(s.state().last_sample[0] == 51, "release PCM");
    s.tick();
    check(s.state().last_sample[0] == 0 && s.state().voices[0].phase == SpuEnvelope::off, "release off");
}

void replay() {
    Spu s;
    upload(s, 0x1c, 3);
    setup(s);
    wr(s, 0x1a0, 1);
    s.tick();
    s.tick();
    auto mid = s.state();
    check(mid.voices[0].cursor == 2 && mid.voices[0].envelope == 28672, "midblock");
    for (int i = 0; i<67; ++i)s.tick();
    auto end = s.state();
    s.restore(mid);
    for (int i = 0; i<67; ++i)s.tick();
    check(s.state() == end, "voice replay");
    auto malformed = mid;
    malformed.voices[0].pitch = 0x2000;
    malformed.voices[0].cursor = 27;
    rejects([&] { s.restore(malformed); });
    check(s.state() == end, "invalid fixed pitch restore atomic");
    Spu t;
    wr(t, 0x1ac, 0x1234);
    wr(t, 0x1ac, 0xabcd);
    wr(t, 0x19a, 0x8010);
    t.tick();
    auto pending = t.state();
    t.tick();
    auto done = t.state();
    t.restore(pending);
    t.tick();
    check(t.state() == done && done.ram[3] == 0xab, "transfer replay");
    auto bad = done;
    bad.transfer_cursor = 1;
    rejects([&] {
        t.restore(bad);
    });
    check(t.state() == done, "atomic restore");
}

void filters() {
    // Independently calculated first two samples for positive nibble 7, shift 12.
    constexpr int second[] {
        7, 14, 20, 18, 20
    };
    constexpr int third[] {
        7, 20, 37, 29, 39
    };
    for (unsigned f = 0; f<5; ++f) {
        Spu s;
        upload(s, (f<<4)|12, 1);
        setup(s);
        wr(s, 0x1a0, 1);
        s.tick();
        check(s.state().voices[0].decoded[0] == 7 && s.state().voices[0].decoded[1] == second[f], "ADPCM filters");
        check(s.state().voices[0].decoded[2] == third[f], "ADPCM second history coefficient");
        for (unsigned i = 1; i<28; ++i)s.tick();
        check(s.state().voices[0].phase == SpuEnvelope::off && s.state().endx == 1, "one-shot end");
    }
    Spu s;
    upload(s, 0x50, 1);
    setup(s);
    wr(s, 0x1a0, 1);
    const auto before = s.state().voices;
    s.tick();
    check(s.state().stop.has_value() && s.state().voices == before, "invalid decoder stops atomically");
}

void adsr() {
    Spu s;
    upload(s, 12, 3);
    setup(s);
    wr(s, 6, 0);
    wr(s, 8, 0x4000);
    wr(s, 0x1a0, 1);
    s.tick();
    check(s.state().voices[0].envelope == 14336, "attack 1");
    s.tick();
    s.tick();
    check(s.state().voices[0].envelope == 32767, "attack peak");
    s.tick();
    check(s.state().voices[0].envelope == 16383, "exponential decay");
    s.tick();
    s.tick();
    s.tick();
    check(s.state().voices[0].envelope == 2047, "decay threshold");
    s.tick();
    check(s.state().voices[0].phase == SpuEnvelope::sustain, "sustain phase");
    s.tick();
    check(s.state().voices[0].envelope == 0, "linear decreasing sustain");
    wr(s, 8, 0);
    s.tick();
    check(s.state().voices[0].envelope == 14336, "linear increasing sustain");
    wr(s, 0x1a4, 1);
    s.tick();
    check(s.state().voices[0].phase == SpuEnvelope::off, "release");
}

void mixing_and_rates() {
    Spu s;
    upload(s, 0, 3);
    setup(s);
    wr(s, 16, 0x3fff);
    wr(s, 18, 0x3fff);
    wr(s, 20, 0x1000);
    wr(s, 22, 15);
    wr(s, 24, 0x1fc0);
    wr(s, 0x188, 3);
    wr(s, 0x190, 1);
    wr(s, 0x1a0, 3);
    s.tick();
    s.tick();
    s.tick();
    check(s.state().last_sample[0] == 32767 && s.state().last_sample[1] == 28667, "two-voice stereo mix");
    wr(s, 16, 0x4000);
    wr(s, 18, 0x4000);
    s.tick();
    check(s.state().last_sample[0] == -3, "signed direct volume cancellation");
    rejects([&] {
        wr(s, 4, 0x2000);
    });
    Spu slow;
    upload(slow, 12, 3);
    setup(slow);
    wr(slow, 6, 0x300f);
    wr(slow, 0x1a0, 1);
    slow.tick();
    check(slow.state().voices[0].envelope == 0 && slow.state().voices[0].envelope_counter == 16384, "slow attack counter");
    slow.tick();
    check(slow.state().voices[0].envelope == 7, "slow attack step");
    Spu loop;
    wr(loop, 0x1aa, 8);
    upload(loop, 12, 7);
    setup(loop);
    wr(loop, 0x1c2, 8);
    wr(loop, 0x1c6, 16);
    wr(loop, 0x1a0, 1);
    loop.tick();
    check(loop.state().voices[0].loop == 8, "loop-start capture");
}

void negative_decode() {
    for (unsigned filter = 0; filter<2; ++filter) {
        Spu s;
        wr(s, 0x1ac, (filter<<4)|0x100);
        for (unsigned i = 0; i<7; ++i)wr(s, 0x1ac, 0x8888);
        wr(s, 0x19a, 0x8010);
        for (unsigned i = 0; i<8; ++i)s.tick();
        setup(s);
        wr(s, 0x1a0, 1);
        s.tick();
        check(s.state().voices[0].decoded[0] == -32768 && s.state().voices[0].decoded[1] == -32768, "negative nibble and predictor clipping");
    }
}

void bounds() {
    Spu s;
    auto original = s.state();
    rejects([&] {
        wr(s, 4, 0x1234);
    });
    rejects([&] {
        wr(s, 6, 0x8000);
    });
    rejects([&] {
        wr(s, 8, 0x20);
    });
    rejects([&] {
        wr(s, 0x19a, 0xc020);
    });
    rejects([&] {
        wr(s, 0x188, 4);
    });
    rejects([&] {
        wr(s, 0, 0x8000);
    });
    check(s.state() == original, "invalid register atomic");
    wr(s, 0x1a8, 15);
    wr(s, 0x1aa, 0xffff);
    wr(s, 0x1ac, 1);
    wr(s, 0x1ac, 2);
    auto pending = s.state();
    rejects([&] {
        wr(s, 0x19a, 0x8010);
    });
    check(s.state() == pending, "transfer bounds atomic");
}
}
int main() {
    try {
        pcm();
        replay();
        filters();
        adsr();
        mixing_and_rates();
        negative_decode();
        bounds();
        std::cout<<"SPU diagnostic tests passed\n";
    }
    catch (const std::exception&e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
