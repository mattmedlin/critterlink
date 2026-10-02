#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace critterlink {

enum class Access { fetch, load, store };
enum class MemoryError { alignment, unmapped, translation };

struct MemoryFault : std::runtime_error {
    MemoryFault(MemoryError reason, Access access, std::uint32_t address);
    MemoryError reason;
    Access access;
    std::uint32_t address;
};

// Bootstrap RAM bus: low physical RAM window and kernel direct-map aliases.
// No TLB, privilege checks, cache, scratchpad, firmware, or devices yet.
class Memory {
public:
    static constexpr std::size_t ram_size = 32 * 1024 * 1024;
    Memory();
    std::uint64_t read(std::uint32_t address, unsigned width, Access access = Access::load) const;
    void write(std::uint32_t address, unsigned width, std::uint64_t value);
    void clear() noexcept;
    std::span<const std::uint8_t> bytes() const noexcept;

private:
    std::size_t resolve(std::uint32_t address, unsigned width, Access access) const;
    std::vector<std::uint8_t> ram_;
};

} // namespace critterlink
