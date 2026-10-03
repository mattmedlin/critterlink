#include "critterlink/integrated_demo.hpp"

#include <algorithm>
#include <stdexcept>

namespace critterlink {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::uint32_t instruction(unsigned op, unsigned rs, unsigned rt, std::uint16_t value) {
    return (op << 26) | (rs << 21) | (rt << 16) | value;
}

struct Program {
    std::vector<std::uint32_t> words;
    void constant(unsigned reg, std::uint32_t value) {
        words.push_back(instruction(15, 0, reg, static_cast<std::uint16_t>(value >> 16)));
        words.push_back(instruction(13, reg, reg, static_cast<std::uint16_t>(value)));
    }
    void small_write(unsigned store_opcode, std::uint16_t offset, std::uint16_t value) {
        words.push_back(instruction(13, 0, 2, value));
        words.push_back(instruction(store_opcode, 1, 2, offset));
    }
    void word_write(std::uint16_t offset, std::uint32_t value) {
        constant(2, value);
        words.push_back(instruction(43, 1, 2, offset));
    }
    void idle() {
        const auto destination = static_cast<std::uint32_t>(words.size());
        words.push_back(0x08000000 | destination);
        words.push_back(0);
    }
    void serial_setup() {
        constant(1, 0x1f808200);
        small_write(43, 0x68, 0x3bc);
        word_write(0x40, 0xffc00505);
        word_write(0x44, 0x2000a);
        word_write(0, (5U << 18) | (5U << 8) | 0x40);
        for (const auto byte : std::array<std::uint16_t, 5>{1, 0x42, 0, 0, 0}) {
            small_write(40, 0x60, byte);
        }
    }
    void serial_read(std::uint16_t destination, std::uint16_t decision) {
        // Fixed transfer length is five logical byte ticks. The wait is guest code.
        for (unsigned i = 0; i < 5; ++i) words.push_back(0);
        for (unsigned i = 0; i < 5; ++i) {
            words.push_back(instruction(36, 1, 3, 0x64));
            words.push_back(0); // IOP load delay.
            words.push_back(instruction(40, 0, 3, static_cast<std::uint16_t>(destination + i)));
        }
        // The guest consumes Cross state: zero means pressed, 64 means released.
        words.push_back(instruction(12, 3, 3, 0x40));
        words.push_back(instruction(43, 0, 3, decision));
    }
};

struct Fixture {
    Program ee;
    Program iop;
    IntegratedDemoTiming timing;
};

Fixture fixture() {
    Fixture f;
    auto& iop = f.iop;
    iop.constant(1, 0x1f900000);
    iop.small_write(41, 0x1a8, 0);
    iop.small_write(41, 0x1aa, 0x1000);
    iop.small_write(41, 0x1ac, 0x031c); // Filter 1, shift 12, repeat/end.
    for (unsigned i = 0; i < 7; ++i) iop.small_write(41, 0x1ac, 0x7777);
    iop.small_write(41, 0x19a, 0x8010);
    for (unsigned i = 0; i < 8; ++i) iop.words.push_back(0);
    iop.small_write(41, 0x19a, 0xc000);
    iop.small_write(41, 0, 0x3fff);
    iop.small_write(41, 2, 0x3fff);
    iop.small_write(41, 4, 0x1000);
    iop.small_write(41, 6, 15);
    iop.small_write(41, 8, 0x1fc0);
    iop.small_write(41, 0x1c0, 0);
    iop.small_write(41, 0x1c2, 0x1000);
    iop.small_write(41, 0x1c4, 0);
    iop.small_write(41, 0x1c6, 0x1000);
    iop.small_write(41, 0x188, 1);
    iop.small_write(41, 0x190, 1);
    iop.small_write(41, 0x198, 0xff0);
    iop.small_write(41, 0x760, 0x3fff);
    iop.small_write(41, 0x762, 0x3fff);

    iop.serial_setup();
    iop.constant(1, 0x1f900000);
    iop.small_write(41, 0x1a0, 1);
    f.timing.key_on_tick = iop.words.size();
    iop.constant(1, 0x1f808200);
    iop.small_write(43, 0x68, 0x3bd);
    const auto serial_start_tick = iop.words.size();
    f.timing.checkpoint_tick = serial_start_tick + 2;
    iop.serial_read(0x6000, 0x6010);
    iop.constant(1, 0x1f900000);
    // Key-off at sample 41; all first-poll work fits inside the first forty samples.
    require(iop.words.size() <= f.timing.key_on_tick + 38, "Integrated IOP setup exceeds audio window");
    while (iop.words.size() < f.timing.key_on_tick + 38) iop.words.push_back(0);
    iop.small_write(41, 0x1a4, 1);
    iop.serial_setup();
    iop.small_write(43, 0x68, 0x3bd);
    iop.serial_read(0x6020, 0x6030);
    iop.small_write(43, 0x80, 1);
    iop.words.push_back(instruction(13, 0, 2, 0x1a15));
    iop.words.push_back(instruction(43, 0, 2, 0x6040));
    iop.idle();
    f.timing.end_tick = iop.words.size() + 12;

    auto& ee = f.ee;
    ee.constant(1, 0x1000e000);
    ee.small_write(43, 0, 1); // D_CTRL enable.
    ee.constant(1, 0x1000a000);
    ee.small_write(43, 0x10, 0x2000);
    ee.small_write(43, 0x20, 6);
    // The first five qwords establish a sprite and its first vertex. Its final
    // vertex remains in flight when SIO2 has emitted three bytes.
    const auto dma_start_tick = serial_start_tick - 2;
    while (ee.words.size() < dma_start_tick - 2) ee.words.push_back(0);
    ee.small_write(43, 0, 0x101);
    ee.idle();
    require(ee.words.size() * 4 < 0x2000 && iop.words.size() * 4 < 0x6000,
            "Integrated fixture code overlaps its data");
    return f;
}

constexpr std::array<std::array<std::uint32_t, 4>, 6> sprite_packet{{
    {0x8005, 0x10000000, 14, 0},
    {6, 0, 0, 0},
    {0x10000, 0, 0x4c, 0},
    {0x80402010, 0, 1, 0},
    {0x00300020, 0, 5, 0},
    {0x00700050, 0, 5, 0}
}};

constexpr std::array<std::int16_t, 40> pcm_oracle{
    2, 11, 18, 24, 29, 34, 39, 43, 47, 51, 55, 58, 61, 64, 67, 70, 73, 75, 77, 79,
    81, 83, 85, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103
};

std::array<std::int16_t, 2> expected_sample(std::uint64_t tick, std::uint64_t key_on) {
    if (tick < key_on) return {0, 0};
    const auto position = tick - key_on;
    if (position < pcm_oracle.size()) return {pcm_oracle[position], pcm_oracle[position]};
    if (position == 40) return {51, 51};
    return {0, 0};
}

bool concurrent(const SystemState& state) {
    const auto& hardware = state.memory.hardware;
    const auto& voice = hardware.spu.voices[0];
    return (hardware.dma.chcr & 0x100) && hardware.dma.qwords == 1 &&
        hardware.graphics.vertex_pending && hardware.sio2.active &&
        hardware.sio2.byte_cursor == 3 && voice.loaded && voice.cursor > 0 &&
        voice.history1 != 0 && voice.envelope != 0 && voice.phase != SpuEnvelope::off &&
        state.input_cursor == 1;
}
} // namespace

IntegratedDemoTiming integrated_demo_timing() {
    return fixture().timing;
}

std::vector<InputEvent> integrated_demo_input() {
    return {{0, 0, {0x4000, {128, 128, 128, 128}}},
            {fixture().timing.checkpoint_tick + 1, 0, {8, {17, 34, 51, 68}}}};
}

void prepare_integrated_demo(System& system) {
    const auto programs = fixture();
    for (std::size_t i = 0; i < programs.ee.words.size(); ++i) {
        system.memory().write(static_cast<std::uint32_t>(i * 4), 4, programs.ee.words[i]);
    }
    for (std::size_t i = 0; i < programs.iop.words.size(); ++i) {
        system.memory().iop().write32(static_cast<std::uint32_t>(i * 4), programs.iop.words[i]);
    }
    for (std::size_t i = 0; i < sprite_packet.size(); ++i) {
        for (unsigned word = 0; word < 4; ++word) {
            system.memory().write(0x2000 + static_cast<std::uint32_t>(i * 16) + word * 4,
                                  4, sprite_packet[i][word]);
        }
    }
    system.memory().iop().start();
}

IntegratedDemoResult run_integrated_demo() {
    System system(integrated_demo_input());
    prepare_integrated_demo(system);
    const auto timing = integrated_demo_timing();
    std::vector<std::array<std::int16_t, 2>> first_pcm, replay_pcm;
    std::vector<InstructionTrace> first_trace, replay_trace;
    std::uint64_t independent_signature = 14695981039346656037ULL;
    const auto step = [&](std::vector<std::array<std::int16_t, 2>>& pcm,
                          std::vector<InstructionTrace>* trace) {
        require(system.run(1, trace) == RunResult{1, true}, "Integrated guest stopped");
        const auto tick = system.memory().hardware().now();
        const auto expected = expected_sample(tick, timing.key_on_tick);
        const auto actual = system.memory().hardware().spu().last_sample;
        require(actual == expected, "Integrated PCM differs from independent literal oracle");
        pcm.push_back(actual);
        for (const auto sample : expected) {
            const auto encoded = static_cast<std::uint16_t>(sample);
            for (unsigned byte = 0; byte < 2; ++byte) {
                independent_signature ^= (encoded >> (byte * 8)) & 0xff;
                independent_signature *= 1099511628211ULL;
            }
        }
    };
    while (system.memory().hardware().now() < timing.checkpoint_tick) step(first_pcm, nullptr);
    const auto checkpoint = system.state();
    require(concurrent(checkpoint), "Integrated snapshot lacks concurrent DMA/audio/input");
    const auto checkpoint_signature = independent_signature;
    const auto checkpoint_samples = first_pcm.size();
    while (system.memory().hardware().now() < timing.end_tick) step(first_pcm, &first_trace);
    const auto completed = system.state();
    require(completed.memory.hardware.spu.signature == independent_signature,
            "Integrated full-run PCM signature differs");

    std::array<std::uint32_t, 64 * 64> expected_pixels{};
    for (unsigned y = 3; y < 7; ++y) {
        for (unsigned x = 2; x < 5; ++x) expected_pixels[y * 64 + x] = 0x80402010;
    }
    require(completed.memory.hardware.graphics.pixels == expected_pixels,
            "Integrated framebuffer differs from exact sprite oracle");
    const auto& iop = system.memory().iop();
    require(iop.read32(0x6000) == 0xff5a41ff && (iop.read32(0x6004) & 0xff) == 0xbf &&
                iop.read32(0x6020) == 0xf75a41ff && (iop.read32(0x6024) & 0xff) == 0xff &&
                iop.read32(0x6010) == 0 && iop.read32(0x6030) == 0x40 &&
                iop.read32(0x6040) == 0x1a15 && completed.input_cursor == 2,
            "Integrated guest input consumption differs from scheduled oracle");

    system.restore(checkpoint);
    independent_signature = checkpoint_signature;
    while (system.memory().hardware().now() < timing.end_tick) step(replay_pcm, &replay_trace);
    const bool replay = system.state() == completed && first_trace == replay_trace &&
        replay_pcm.size() == first_pcm.size() - checkpoint_samples &&
        std::equal(replay_pcm.begin(), replay_pcm.end(), first_pcm.begin() + checkpoint_samples);
    require(replay, "Integrated replay changed CPU/device/output state");
    return {timing.end_tick, completed.memory.hardware.spu.sample_count,
            completed.memory.hardware.spu.signature, 12, true, true, true, true, replay};
}

} // namespace critterlink
