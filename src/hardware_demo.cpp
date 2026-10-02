#include "critterlink/hardware_demo.hpp"

#include <algorithm>
#include <stdexcept>

namespace critterlink {

void prepare_hardware_demo(System& system) {
    constexpr std::array<std::uint32_t, 22> program{
        0x3c011000, // LUI r1,0x1000: timer base
        0x24020006, // ADDIU r2,r0,6
        0xac220020, // SW r2,T0_COMP(r1)
        0x24020180, // CUE | CMPE
        0xac220010, // SW r2,T0_MODE(r1)
        0x24020200, // INTC timer0 mask
        0x3423f000, // ORI r3,r1,0xf000: INTC
        0xac620010, // SW r2,INTC_MASK
        0x3423e000, // ORI r3,r1,0xe000: DMAC
        0x24020001, // DMA enable
        0xac620000, // SW r2,D_CTRL
        0x3c020004, // channel2 mask bit18
        0xac620010, // SW r2,D_STAT
        0x3423a000, // ORI r3,r1,0xa000: GIF channel2
        0x24022000, // source RAM at 0x2000
        0xac620010, // SW r2,D2_MADR
        0x24020006, // six qwords
        0xac620020, // SW r2,D2_QWC
        0x24020101, // start normal RAM-to-GIF transfer
        0xac620000, // SW r2,D2_CHCR; first qword arrives at tick20
        0x08000014, // J to self at 0x50
        0x00000000  // NOP delay slot
    };
    constexpr std::array<std::array<std::uint32_t, 4>, 6> packet{{
        {0x8005, 0x10000000, 14, 0}, // GIF PACKED A+D, five register writes
        {6, 0, 0, 0},              // GS PRIM: sprite
        {0x10000, 0, 0x4c, 0},     // GS FRAME: 64px PSMCT32, base0
        {0x80402010, 0, 1, 0},     // GS RGBAQ
        {0x00300020, 0, 5, 0},     // GS XYZ2: (2,3)
        {0x00700050, 0, 5, 0}      // GS XYZ2: (5,7)
    }};
    for (std::size_t n = 0; n < program.size(); ++n) {
        system.memory().write(static_cast<std::uint32_t>(n * 4), 4, program[n]);
    }
    for (std::size_t n = 0; n < packet.size(); ++n) {
        for (unsigned word = 0; word < 4; ++word) {
            system.memory().write(0x2000u + static_cast<std::uint32_t>(n * 16) + word * 4, 4, packet[n][word]);
        }
    }
}

HardwareDemoResult run_hardware_demo() {
    System system({{0, 0, {0x4000, {128, 128, 128, 128}}}, {26, 0, {}}});
    prepare_hardware_demo(system);
    if (system.run(24) != RunResult{24, true}) { throw std::runtime_error("hardware fixture failed before snapshot"); }
    system.pad(0).select();
    const auto first = system.pad(0).exchange(1);
    const auto second = system.pad(0).exchange(0x42);
    const auto checkpoint = system.state();
    const auto finish = [&] {
        if (system.run(40) != RunResult{40, true}) { throw std::runtime_error("hardware fixture failed after snapshot"); }
        return std::array<std::uint8_t, 5>{first, second, system.pad(0).exchange(0),
                                         system.pad(0).exchange(0), system.pad(0).exchange(0)};
    };
    // Both runs use the same finish boundary; complete device state is compared.
    const auto reply = finish();
    const auto completed = system.state();
    system.restore(checkpoint);
    const auto replay_reply = finish();
    const auto& graphics = system.memory().hardware().graphics();
    constexpr std::array<std::uint8_t, 16> adpcm{0, 0, 0x87, 0xf1};
    return {system.memory().hardware().now(),
            static_cast<std::size_t>(std::count(graphics.pixels.begin(), graphics.pixels.end(), 0x80402010u)),
            graphics.pixels[3 * 64 + 2], system.memory().hardware().int0(), system.memory().hardware().int1(),
            system.state() == completed && reply == replay_reply, reply, decode_adpcm_filter_zero(adpcm)};
}

} // namespace critterlink
