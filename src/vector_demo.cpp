#include "critterlink/vector_demo.hpp"

#include <stdexcept>

namespace critterlink {
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
}

void prepare_vector_demo(System& system) {
    // Original microprogram, stored lower word first. Encodings follow Sony's
    // VU manual: IADDIU, LQ, SQ, upper NOP and E. Padding separates dependent
    // operations; diagnostic execution does not model hardware pipelines.
    constexpr std::array<std::uint32_t, 36> packet{
        0,                      // VIF NOP aligns the MPG payload to 64 bits
        0x4a0d0000,             // MPG: 13 instruction pairs at micro address0
        0x10010001, 0x000002ff, // IADDIU VI1,VI0,1 | NOP
        0x01e10000, 0x000002ff, // LQ.xyzw VF1,0(VI0) | NOP
        0x10000000, 0x000002ff, // IADDIU VI0,VI0,0 | NOP (hazard padding)
        0x10000000, 0x000002ff,
        0x10000000, 0x000002ff,
        0x10000000, 0x000002ff,
        0x03e10800, 0x000002ff, // SQ.xyzw VF1,0(VI1) | NOP
        0x10000000, 0x000002ff,
        0x10000000, 0x000002ff,
        0x10000000, 0x000002ff,
        0x10000000, 0x000002ff,
        0x10000000, 0x400002ff, // E schedules termination after one delay pair
        0x10000000, 0x000002ff,
        0x6c010000,             // UNPACK V4_32: one vector at data address0
        7, 9, 11, 13,
        0x14000000,             // MSCAL: start VU1 at micro address0
        0x10000000,             // FLUSHE: wait until VU1 finishes
        0                      // VIF NOP padding to whole DMA qwords
    };
    constexpr std::array<std::uint32_t, 15> program{
        0x3c011000, // LUI r1,0x1000
        0x3423e000, // ORI r3,r1,0xe000: global DMAC
        0x24020001, // ADDIU r2,zero,DMAE
        0xac620000, // SW r2,D_CTRL
        0x3c020002, // LUI r2,2: channel1 interrupt mask
        0xac620010, // SW r2,D_STAT
        0x34239000, // ORI r3,r1,0x9000: VIF1 channel
        0x24022000, // ADDIU r2,zero,source address
        0xac620010, // SW r2,D1_MADR
        0x24020000u | static_cast<std::uint32_t>(packet.size() / 4),
        0xac620020, // SW r2,D1_QWC
        0x24020101, // ADDIU r2,zero,normal RAM-to-VIF1 start
        0xac620000, // SW r2,D1_CHCR
        0x0800000d, // J 0x34
        0x00000000  // NOP delay slot
    };
    for (std::size_t n = 0; n < program.size(); ++n) {
        system.memory().write(static_cast<std::uint32_t>(n * 4), 4, program[n]);
    }
    for (std::size_t n = 0; n < packet.size(); ++n) {
        system.memory().write(0x2000u + static_cast<std::uint32_t>(n * 4), 4, packet[n]);
    }
}

VectorDemoResult run_vector_demo() {
    System system;
    prepare_vector_demo(system);
    // A FLUSHE command holds a partly consumed DMA qword while VU1 executes.
    for (unsigned n = 0; n < 128 && !system.memory().hardware().state().vif_dma.loaded; ++n) {
        require(system.run(1) == RunResult{1, true}, "vector fixture stopped before FLUSHE checkpoint");
    }
    require(system.memory().hardware().state().vif_dma.loaded, "vector fixture did not reach FLUSHE checkpoint");
    require(system.run(3) == RunResult{3, true}, "vector fixture stopped while VU1 was running");
    const auto checkpoint = system.state();
    require(checkpoint.memory.hardware.vif_dma.loaded && checkpoint.memory.hardware.vector.running &&
            checkpoint.memory.hardware.vector.pc == 3, "vector checkpoint was not mid-VU1 and mid-FLUSHE");
    std::vector<InstructionTrace> first, second;
    require(system.run(64, &first) == RunResult{64, true}, "vector fixture stopped after checkpoint");
    const auto expected = system.state();
    system.restore(checkpoint);
    for (unsigned n = 0; n < 64; ++n) {
        require(system.run(1, &second) == RunResult{1, true}, "vector fixture replay stopped");
    }
    const auto& hardware = system.memory().hardware();
    constexpr std::array<std::uint32_t, 4> expected_output{7, 9, 11, 13};
    require(hardware.vector().data[1] == expected_output && hardware.vector().vi[1] == 1 &&
            !hardware.vector().running, "VU1 output or completion differs from the original fixture");
    return {hardware.now(), hardware.vector().data[1],
            hardware.read(0x10009020) == 0 && (hardware.read(0x10009000) & 0x100u) == 0 &&
            (hardware.read(0x1000e010) & 2u) != 0 && hardware.int1(),
            system.state() == expected && first == second};
}
} // namespace critterlink
