#pragma once

#include <array>
#include <cstdint>

namespace critterlink {

struct SourceChainStack {
    std::array<std::uint32_t, 2> addresses{};
    unsigned depth{};
    bool operator==(const SourceChainStack&) const = default;
};
struct SourceChainPacket {
    std::uint32_t address{}, qwords{}, next_tag{};
    // Unshifted CHCR.TAG: raw tag bits31:16, including IRQ/ID/PCE.
    std::uint16_t tag{};
    SourceChainStack stack;
    bool end{}, stack_overflow{};
    // ID4 identifies REFS; the caller must apply its supported stall policy.
    std::uint8_t id{};
    bool operator==(const SourceChainPacket&) const = default;
};

// Pure RAM-profile planner: no reads, transfer, TIE handling, or mutable state.
// Rejects scratchpad, misalignment, unsupported priority/reserved fields, invalid
// stack depth, and derived addresses outside the lower31-bit address space.
// Actual installed-RAM bounds and bus errors belong to the caller.
// Terminal next_tag retains tadr as a deterministic policy. Stack updates are
// planned at decode time; callers must not apply them twice on packet restart.
SourceChainPacket decode_source_chain(std::uint32_t tadr,
                                     std::array<std::uint32_t, 4> raw,
                                     SourceChainStack stack);

} // namespace critterlink
