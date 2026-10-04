// Published PS2 result facts; sources linked in docs/fpu.md.
#pragma once
#include <array>
#include <cstdint>
inline constexpr std::array<std::array<std::uint32_t,2>, 21> fpu_sqrt_vectors{{
    {0x00000000U,0x00000000U},
    {0x80000000U,0x00000000U},
    {0x3f800000U,0x3f800000U},
    {0x40000000U,0x3fb504f3U},
    {0x40400000U,0x3fddb3d7U},
    {0x7fffffffU,0x5fb504f3U},
    {0xffffffffU,0x5fb504f3U},
    {0x7f800000U,0x5f800000U},
    {0xff800000U,0x5f800000U},
    {0x41c80000U,0x40a00000U},
    {0x00000000U,0x00000000U},
    {0x80000000U,0x00000000U},
    {0x3f800000U,0x3f800000U},
    {0xbf800000U,0x3f800000U},
    {0x3fffffffU,0x3fb504f3U},
    {0x7f800001U,0x5f800000U},
    {0x00000001U,0x00000000U},
    {0x7fffffffU,0x5fb504f3U},
    {0xffffffffU,0x5fb504f3U},
    {0x00001337U,0x00000000U},
    {0xdeadbeefU,0x4f152108U},
}};
