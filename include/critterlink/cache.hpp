#pragma once
#include "critterlink/memory.hpp"

namespace critterlink {
struct CacheLine {
    std::uint32_t tag{};
    std::array<std::uint8_t,64> bytes{};
    bool operator==(const CacheLine&) const = default;
};
struct CacheState {
    std::uint32_t config{0x442},tag_lo{},tag_hi{};
    // Lazy allocation keeps cold CPU snapshots small and reset non-allocating.
    std::vector<CacheLine> instruction,data;
    bool operator==(const CacheState&) const = default;
};
void validate_cache(const CacheState& state);
void write_config(CacheState& state,std::uint32_t value);
bool cache_enabled(const CacheState& state,unsigned mode,bool instruction) noexcept;
std::uint64_t cache_read(CacheState& state,Memory& memory,std::uint32_t va,std::uint32_t pa,unsigned count,bool instruction);
void cache_write(CacheState& state,Memory& memory,std::uint32_t va,std::uint32_t pa,unsigned count,std::uint64_t value,unsigned mode);
bool cache_index_operation(unsigned op) noexcept;
void cache_operation(CacheState& state,Memory& memory,unsigned op,std::uint32_t va,std::uint32_t pa=0);
} // namespace critterlink
