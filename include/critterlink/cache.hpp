#pragma once
#include "critterlink/memory.hpp"
#include <optional>

namespace critterlink {
struct CacheLine {
    std::uint32_t tag{};
    std::array<std::uint8_t,64> bytes{};
    bool operator==(const CacheLine&) const = default;
};
struct AcceleratedBuffer {
    bool valid{};
    std::uint32_t base{};
    std::array<std::uint8_t,128> bytes{};
    bool operator==(const AcceleratedBuffer&) const = default;
};
struct CacheState {
    std::uint32_t config{0x442},tag_lo{},tag_hi{};
    // Lazy allocation keeps cold CPU snapshots small and reset non-allocating.
    std::vector<CacheLine> instruction,data;
    AcceleratedBuffer accelerated;
    bool operator==(const CacheState&) const = default;
};
void validate_cache(const CacheState& state);
void write_config(CacheState& state,std::uint32_t value);
bool accelerated_enabled(const CacheState& state,unsigned mode) noexcept;
std::uint64_t accelerated_read(CacheState& state,Memory& memory,std::uint32_t pa,unsigned count);
bool cache_enabled(const CacheState& state,unsigned mode,bool instruction) noexcept;
std::uint64_t cache_read(CacheState& state,Memory& memory,std::uint32_t va,std::uint32_t pa,unsigned count,bool instruction);
void cache_write(CacheState& state,Memory& memory,std::uint32_t va,std::uint32_t pa,unsigned count,std::uint64_t value,unsigned mode);
bool cache_index_operation(unsigned op) noexcept;
// Only DHIN/DHWBIN report a Status.CH update; other operations return no value.
std::optional<bool> cache_operation(CacheState& state,Memory& memory,unsigned op,std::uint32_t va,std::uint32_t pa=0);
} // namespace critterlink
