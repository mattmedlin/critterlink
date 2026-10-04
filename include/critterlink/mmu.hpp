#pragma once
#include "critterlink/memory.hpp"
#include <array>
#include <optional>

namespace critterlink {
struct TlbEntry {
    std::uint32_t mask{}, hi{}, lo0{}, lo1{};
    bool operator==(const TlbEntry&) const = default;
};
struct MmuState {
    std::uint32_t index{}, random{47}, lo0{}, lo1{}, context{}, mask{}, wired{}, hi{};
    // Reset tags are unspecified by hardware. Choose distinct invalid kernel tags.
    std::array<TlbEntry,48> entries = [] {
        std::array<TlbEntry,48> result{};
        for (std::uint32_t i=0;i<48;++i) result[i].hi=0x80000000U+i*0x2000U;
        return result;
    }();
    bool operator==(const MmuState&) const = default;
};
struct Translation { std::uint32_t address; bool scratchpad{}, global{true}, mapped{}; };
struct TranslationFault { unsigned code; std::uint32_t address; bool refill{}; };
bool kernel_mode(std::uint32_t status) noexcept;
bool valid_page_mask(std::uint32_t mask) noexcept;
void validate_mmu(const MmuState& state);
std::optional<std::uint32_t> read_mmu_register(const MmuState& state,unsigned reg);
bool write_mmu_register(MmuState& state,unsigned reg,std::uint32_t value);
void tlb_read(MmuState& state);
void tlb_write(MmuState& state,unsigned index);
void tlb_probe(MmuState& state);
void advance_random(MmuState& state) noexcept;
Translation translate(const MmuState& state,std::uint32_t status,std::uint32_t address,Access access);
} // namespace critterlink
