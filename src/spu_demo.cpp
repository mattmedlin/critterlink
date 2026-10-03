#include "critterlink/spu_demo.hpp"
#include <stdexcept>
namespace critterlink {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
constexpr std::uint32_t imm(unsigned op, unsigned rs, unsigned rt, std::uint16_t value) {
    return (op << 26U) | (rs << 21U) | (rt << 16U) | value;
}
void half(std::vector<std::uint32_t>& code, std::uint16_t address, std::uint16_t value) {
    code.push_back(imm(13, 0, 2, value));
    code.push_back(imm(41, 1, 2, address));
}
}
void prepare_spu_demo(System& system) {
    // EE waits while the independent IOP programs SPU2 through real halfword accesses.
    system.memory().write(0, 4, 0x08000000);
    system.memory().write(4, 4, 0);
    std::vector<std::uint32_t> code{imm(15, 0, 1, 0x1f90)};
    half(code, 0x1a8, 0);
    half(code, 0x1aa, 0x1000); // Sample RAM word address = byte address 0x2000
    half(code, 0x1ac, 0x031c); // Filter1, shift12, end+repeat
    for (unsigned n = 0; n < 7; ++n) half(code, 0x1ac, 0x7777);
    half(code, 0x19a, 0x8010); // Manual transfer staged FIFO to sound RAM
    const auto transfer_poll = code.size();
    code.push_back(imm(37, 1, 3, 0x344));
    code.push_back(0); // R3000 load delay
    code.push_back(imm(12, 3, 3, 0x0400));
    code.push_back(imm(5, 3, 0, 0xfffc));
    code.push_back(0);
    require(transfer_poll + 5 == code.size(), "invalid SPU transfer poll");
    half(code, 0x19a, 0xc000); // Enable unmuted core0
    half(code, 0, 0x3fff); half(code, 2, 0x3fff);
    half(code, 4, 0x1000); half(code, 6, 0x000f); half(code, 8, 0x1fc0);
    half(code, 0x1c0, 0); half(code, 0x1c2, 0x1000);
    half(code, 0x1c4, 0); half(code, 0x1c6, 0x1000); // Loop address
    half(code, 0x188, 1); half(code, 0x190, 1); half(code, 0x198, 0x0ff0);
    half(code, 0x760, 0x3fff); half(code, 0x762, 0x3fff);
    half(code, 0x1a0, 1);
    // Forty samples exercise an ADPCM loop, decoder history and sustained envelope.
    for (unsigned n = 0; n < 38; ++n) code.push_back(0);
    half(code, 0x1a4, 1);
    const auto idle = static_cast<std::uint32_t>(code.size());
    code.push_back(0x08000000U | idle); code.push_back(0);
    for (std::size_t n = 0; n < code.size(); ++n)
        system.memory().iop().write32(static_cast<std::uint32_t>(n * 4), code[n]);
    system.memory().iop().start();
}
SpuDemoResult run_spu_demo() {
    System system;
    prepare_spu_demo(system);
    std::vector<std::array<std::int16_t, 2>> pcm;
    bool began = false;
    for (unsigned n = 0; n < 256 && pcm.size() < 2; ++n) {
        require(system.run(1) == RunResult{1, true}, "SPU guest fixture stopped during setup");
        const auto& spu = system.memory().hardware().spu();
        began = began || spu.voices[0].phase != SpuEnvelope::off;
        if (began) pcm.push_back(spu.last_sample);
    }
    require(pcm.size() == 2, "SPU guest never keyed on");
    const auto checkpoint = system.state();
    const auto& voice = checkpoint.memory.hardware.spu.voices[0];
    require(voice.loaded && voice.cursor == 2 && voice.history1 != 0 &&
        voice.phase == SpuEnvelope::attack && voice.envelope == 28672,
        "SPU checkpoint lacks decoder, pitch cursor or attack state");
    std::vector<InstructionTrace> first_trace, replay_trace;
    std::vector<std::array<std::int16_t, 2>> tail, replay;
    for (unsigned n = 0; n < 64; ++n) {
        require(system.run(1, &first_trace) == RunResult{1, true}, "SPU guest playback stopped");
        tail.push_back(system.memory().hardware().spu().last_sample);
    }
    const auto expected = system.state();
    system.restore(checkpoint);
    for (unsigned n = 0; n < 64; ++n) {
        require(system.run(1, &replay_trace) == RunResult{1, true}, "SPU playback replay stopped");
        replay.push_back(system.memory().hardware().spu().last_sample);
    }
    pcm.insert(pcm.end(), tail.begin(), tail.end());
    // Independently calculated recurrence: x[n]=7+floor((60*x[n-1]+32)/64).
    // Envelope: 14336,28672,32767; direct voice/master gains are 32766/32768.
    constexpr std::array<std::int16_t, 40> oracle{
        2,11,18,24,29,34,39,43,47,51,55,58,61,64,67,70,73,75,77,79,
        81,83,85,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103
    };
    for (std::size_t n = 0; n < oracle.size(); ++n)
        require(pcm[n] == std::array<std::int16_t, 2>{oracle[n], oracle[n]},
            "SPU PCM differs from independent integer oracle");
    require(pcm[40] == std::array<std::int16_t, 2>{51,51}, "SPU release ramp differs");
    for (std::size_t n = 41; n < pcm.size(); ++n)
        require(pcm[n] == std::array<std::int16_t, 2>{0,0}, "SPU key-off did not finish");
    const auto& spu = system.memory().hardware().spu();
    return {system.memory().hardware().now(), spu.sample_count, spu.signature, true,
        spu.voices[0].phase == SpuEnvelope::off,
        system.state() == expected && tail == replay && first_trace == replay_trace};
}
} // namespace critterlink
