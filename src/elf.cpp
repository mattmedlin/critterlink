#include "critterlink/elf.hpp"

#include <string>
#include <utility>
#include <vector>

namespace critterlink {
namespace {
struct Segment {
    std::uint32_t offset, address, file_size, memory_size, flags;
};
} // namespace

ElfImage load_elf(std::span<const std::uint8_t> file, Memory& memory, Cpu& cpu) {
    const auto require = [](bool valid, const std::string& reason) {
        if (!valid) { throw ElfError("ELF: " + reason); }
    };
    const auto range = [&](std::uint32_t offset, std::uint64_t length) {
        return offset <= file.size() && length <= file.size() - offset;
    };
    require(file.size() >= 52 && file.size() <= maximum_elf_size, "file must be 52 bytes through 64 MiB");
    const auto u16 = [&](std::size_t offset) {
        return std::uint32_t{file[offset]} | (std::uint32_t{file[offset + 1]} << 8);
    };
    const auto u32 = [&](std::size_t offset) {
        return std::uint32_t{file[offset]} | (std::uint32_t{file[offset + 1]} << 8) |
               (std::uint32_t{file[offset + 2]} << 16) | (std::uint32_t{file[offset + 3]} << 24);
    };
    require(file[0] == 0x7f && file[1] == 'E' && file[2] == 'L' && file[3] == 'F', "bad magic");
    require(file[4] == 1 && file[5] == 1, "only ELF32 little-endian is supported");
    require(file[6] == 1 && u32(20) == 1, "invalid ELF version");
    require(file[7] == 0 && file[8] == 0, "only System V ABI version zero is supported");
    require(u16(16) == 2 && u16(18) == 8, "expected static ET_EXEC for EM_MIPS");
    require(u16(40) == 52 && u16(42) == 32, "invalid ELF/program header size");
    const auto flags = u32(36);
    require((flags & ~1u) == 0x20000000u,
            "only MIPS III flags with optional NOREORDER are supported (no PIC/compressed ISA)");
    const auto phoff = u32(28);
    const auto count = u16(44);
    require(count > 0 && count <= 128, "program header count must be 1 through 128");
    require(phoff >= 52 && range(phoff, std::uint64_t{count} * 32), "truncated program header table");

    // Sections are not loaded, but validate their table and file-backed ranges
    // when present. Extended numbering and relocations are outside this profile.
    const auto shoff = u32(32);
    const auto shcount = u16(48);
    const auto shstrings = u16(50);
    if (shoff == 0) {
        require(shcount == 0 && shstrings == 0, "section metadata without a section table");
    } else {
        require(shoff >= 52 && shcount > 0 && u16(46) == 40 &&
                range(shoff, std::uint64_t{shcount} * 40), "invalid/truncated section header table");
        require(shstrings == 0 || shstrings < shcount, "invalid section name table index");
        for (std::uint32_t index = 0; index < shcount; ++index) {
            const auto at = std::size_t{shoff} + index * 40u;
            const auto type = u32(at + 4);
            require(type != 4 && type != 9 && type != 6, "relocations/dynamic sections are unsupported");
            if (type != 0 && type != 8) {
                require(range(u32(at + 16), u32(at + 20)), "truncated section contents");
            }
        }
    }

    const auto entry = u32(24);
    require(entry % 4 == 0, "entry point must be instruction-aligned");
    std::vector<Segment> segments;
    ElfImage image{entry, 0, 0, 0};
    bool executable_entry = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto at = std::size_t{phoff} + index * 32u;
        const auto type = u32(at);
        if (type == 0) { continue; }
        require(type == 1 || type == 4 || type == 6, "unsupported program header type " + std::to_string(type));
        const Segment segment{u32(at + 4), u32(at + 8), u32(at + 16), u32(at + 20), u32(at + 24)};
        require(range(segment.offset, segment.file_size), "truncated segment " + std::to_string(index));
        if (type != 1) { continue; } // validated NOTE/PHDR metadata, no runtime behavior
        require(segment.file_size <= segment.memory_size, "segment file size exceeds memory size");
        require((segment.flags & ~7u) == 0, "unsupported segment flags");
        const auto alignment = u32(at + 28);
        require(alignment <= 1 || ((alignment & (alignment - 1)) == 0 &&
                segment.address % alignment == segment.offset % alignment), "invalid segment alignment");
        require(segment.address <= Memory::ram_size && segment.memory_size <= Memory::ram_size - segment.address,
                "segment outside bootstrap RAM");
        if (segment.memory_size == 0) { continue; }
        for (const auto& prior : segments) {
            require(segment.address >= prior.address + prior.memory_size ||
                    prior.address >= segment.address + segment.memory_size, "overlapping load segments");
        }
        if ((segment.flags & 1u) != 0 && entry >= segment.address &&
            std::uint64_t{entry} + 4 <= std::uint64_t{segment.address} + segment.file_size) {
            executable_entry = true;
        }
        segments.push_back(segment);
        image.file_bytes += segment.file_size;
        image.memory_bytes += segment.memory_size;
    }
    require(!segments.empty() && executable_entry, "entry is not inside file-backed executable code");
    image.segments = static_cast<std::uint32_t>(segments.size());

    // Stage the full update. Even allocation failures or an input span aliasing
    // existing RAM cannot cause partial writes to the caller's state.
    Memory staged = memory;
    for (const auto& segment : segments) {
        for (std::uint32_t n = 0; n < segment.memory_size; ++n) {
            staged.write(segment.address + n, 1, n < segment.file_size ? file[segment.offset + n] : 0);
        }
    }
    memory = std::move(staged);
    cpu.reset(entry);
    return image;
}

} // namespace critterlink
