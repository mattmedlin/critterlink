#pragma once

#include "critterlink/memory.hpp"

#include <array>
#include <cstdint>

namespace critterlink {

// Hand-authored instruction fixture; independently specified in cpu-coverage.md.
inline constexpr std::array<std::uint32_t, 9> demo_program{
    0x24010005, // addiu r1, r0, 5
    0x24020000, // addiu r2, r0, 0
    0x00411021, // addu  r2, r2, r1
    0x2421ffff, // addiu r1, r1, -1
    0x1420fffd, // bne   r1, r0, loop (address 8)
    0x24630001, // addiu r3, r3, 1 (delay slot, including final untaken branch)
    0xac020100, // sw    r2, 0x100(r0)
    0x8c040100, // lw    r4, 0x100(r0)
    0x34850080, // ori   r5, r4, 0x80
};
inline constexpr std::uint64_t demo_instruction_count = 25;

inline void load_demo(Memory& memory) {
    for (std::size_t i = 0; i < demo_program.size(); ++i) {
        memory.write(static_cast<std::uint32_t>(i * 4), 4, demo_program[i]);
    }
}

} // namespace critterlink
