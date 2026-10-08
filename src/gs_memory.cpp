#include "critterlink/gs_memory.hpp"
#include <array>
#include <stdexcept>

namespace critterlink {
std::uint32_t gs_psmct32_address(std::uint32_t bp, std::uint32_t bw,
                                std::uint32_t x, std::uint32_t y) {
    if (bp >= 16384 || bw == 0 || bw > 32 || x >= 2048 || y >= 2048) {
        throw std::invalid_argument("invalid GS PSMCT32 buffer or coordinates");
    }
    // Original transcription of GS User's Manual v6.0 p164, PSMCT32 diagram.
    constexpr std::array<std::array<unsigned, 8>, 4> blocks{{
        {0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23},
        {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}
    }};
    constexpr std::array<std::array<unsigned, 8>, 2> words{{
        {0, 1, 4, 5, 8, 9, 12, 13}, {2, 3, 6, 7, 10, 11, 14, 15}
    }};
    const auto page = std::uint64_t{y / 32} * bw + x / 64;
    const auto address = std::uint64_t{bp} * 256 + page * 8192 +
        blocks[(y % 32) / 8][(x % 64) / 8] * 256 + ((y % 8) / 2) * 64 +
        words[y % 2][x % 8] * 4;
    if (address >= 4U * 1024U * 1024U) {
        throw std::invalid_argument("GS PSMCT32 address exceeds local memory; wrapping unsupported");
    }
    return static_cast<std::uint32_t>(address);
}
} // namespace critterlink
