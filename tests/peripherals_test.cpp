#include "critterlink/peripherals.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <typename F> void rejects(F function) {
    try { function(); } catch (const std::logic_error&) { return; }
    throw std::runtime_error("expected rejection");
}
void test_pad() {
    critterlink::DigitalPad pad;
    rejects([&] { (void)pad.exchange(1); });
    pad.set_buttons(0x4008); // Cross + Start.
    pad.select();
    check(pad.exchange(1) == 0xff, "peripheral response");
    const auto before = pad.state();
    rejects([&] { (void)pad.exchange(0x43); });
    check(pad.state() == before, "unsupported command changed state");
    check(pad.exchange(0x42) == 0x41, "digital ID");
    const auto snapshot = pad.state();
    pad.set_buttons(0); // A host change cannot tear the current packet.
    check(pad.exchange(0) == 0x5a, "response marker");
    check(pad.exchange(0) == 0xf7, "Start active-low");
    check(pad.exchange(0) == 0xbf, "Cross active-low");
    rejects([&] { (void)pad.exchange(0); });
    pad.restore(snapshot);
    check(pad.exchange(0) == 0x5a && pad.exchange(0) == 0xf7 &&
          pad.exchange(0) == 0xbf, "midpacket restoration");
    pad.deselect();
    pad.set_buttons(0);
    pad.select();
    constexpr std::array<std::uint8_t, 5> tx{1, 0x42, 0, 0, 0};
    constexpr std::array<std::uint8_t, 5> rx{0xff, 0x41, 0x5a, 0xff, 0xff};
    for (std::size_t i = 0; i < tx.size(); ++i) check(pad.exchange(tx[i]) == rx[i], "released poll");
    auto invalid = pad.state();
    invalid.position = 6;
    rejects([&] { pad.restore(invalid); });
}
void test_audio() {
    std::array<std::uint8_t, 16> block{};
    block[1] = 7;
    block[2] = 0x87;
    block[3] = 0xf1;
    auto pcm = critterlink::decode_adpcm_filter_zero(block);
    check(pcm.samples[0] == 28672 && pcm.samples[1] == -32768 &&
          pcm.samples[2] == 4096 && pcm.samples[3] == -4096, "signed nibbles and byte order");
    for (std::size_t i = 4; i < pcm.samples.size(); ++i) check(pcm.samples[i] == 0, "zero samples");
    check(pcm.loop_end && pcm.loop_repeat && pcm.loop_start, "loop flags");
    block[0] = 12;
    pcm = critterlink::decode_adpcm_filter_zero(block);
    check(pcm.samples[0] == 7 && pcm.samples[1] == -8 && pcm.samples[2] == 1 && pcm.samples[3] == -1, "maximum supported shift");
    check(pcm == critterlink::decode_adpcm_filter_zero(block), "deterministic decode");
    rejects([&] { (void)critterlink::decode_adpcm_filter_zero(std::span(block).first(15)); });
    block[0] = 0x10;
    rejects([&] { (void)critterlink::decode_adpcm_filter_zero(block); });
    block[0] = 13;
    rejects([&] { (void)critterlink::decode_adpcm_filter_zero(block); });
    block[0] = 0;
    block[1] = 8;
    rejects([&] { (void)critterlink::decode_adpcm_filter_zero(block); });
}
}
int main() {
    try {
        test_pad();
        test_audio();
        std::cout << "All peripheral diagnostic checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
