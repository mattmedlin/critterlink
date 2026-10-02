#pragma once

#include "critterlink/cpu.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>

namespace critterlink {

class ElfError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ElfImage {
    std::uint32_t entry{};
    std::uint32_t segments{};
    std::uint32_t file_bytes{};
    std::uint32_t memory_bytes{};
};

inline constexpr std::size_t maximum_elf_size = 64 * 1024 * 1024;

// Static ELF32 little-endian MIPS III bootstrap profile. On failure both RAM
// and CPU are unchanged. On success only segment ranges change and CPU resets
// to the validated entry with zero registers (no implicit stack/kernel ABI).
ElfImage load_elf(std::span<const std::uint8_t> file, Memory& memory, Cpu& cpu);

} // namespace critterlink
