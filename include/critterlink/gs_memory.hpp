#pragma once
#include <cstdint>

namespace critterlink {
// Sony GS PSMCT32 page/block/column layout. BP is in 256-byte blocks;
// BW is in 64-pixel units. Returns a byte offset in the 4 MiB local memory.
// This supported profile rejects coordinate or physical-memory wrap.
std::uint32_t gs_psmct32_address(std::uint32_t bp, std::uint32_t bw,
                                std::uint32_t x, std::uint32_t y);
} // namespace critterlink
