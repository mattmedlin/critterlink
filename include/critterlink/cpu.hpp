#pragma once

#include "critterlink/memory.hpp"
#include "critterlink/mmu.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace critterlink {

struct Register128 {
    std::uint64_t low{};
    std::uint64_t high{};
    bool operator==(const Register128&) const = default;
};

enum class StopKind { exception, unsupported_instruction, unsupported_access, delay_slot_branch };

struct CpuStop {
    StopKind kind{};
    std::uint32_t pc{};
    std::optional<std::uint32_t> instruction;
    std::string diagnostic;
    bool operator==(const CpuStop&) const = default;
};

// Implemented COP0 exception/control subset; MMU registers live in MmuState.
struct Cop0State {
    std::uint32_t bad_vaddr{};
    std::uint32_t cause{};
    std::uint32_t epc{};
    std::uint32_t status{};
    std::uint32_t error_epc{};
    bool operator==(const Cop0State&) const = default;
};

struct FpuState {
    std::array<std::uint32_t, 32> fpr{};
    std::uint32_t accumulator{};
    std::uint32_t control{0x01000001U};
    bool operator==(const FpuState&) const = default;
};

struct CpuState {
    std::array<Register128, 32> gpr{};
    Register128 hi{}, lo{};
    // Internal funnel count in bits; guest MFSA/MTSA use a four-bit byte count.
    std::uint64_t sa{};
    std::uint32_t pc{};
    std::uint32_t next_pc{4};
    bool delay_slot{};
    std::uint32_t branch_pc{};
    Cop0State cop0{};
    FpuState fpu{};
    MmuState mmu{};
    // Boot-vector execution uses architectural translation. Original fixtures opt
    // into the existing flat bootstrap profile through the default reset API.
    bool architectural_memory{};
    std::optional<CpuStop> stop;
    bool operator==(const CpuState&) const = default;
};

struct InstructionTrace {
    std::uint32_t pc{};
    std::optional<std::uint32_t> instruction;
    bool delay_slot{};
    bool retired{};
    std::optional<CpuStop> stop;
    std::optional<unsigned> exception;
    bool stalled{};
    bool operator==(const InstructionTrace&) const = default;
};

struct RunResult {
    std::uint64_t retired{};
    bool budget_exhausted{};
    bool operator==(const RunResult&) const = default;
};

class Cpu {
public:
    explicit Cpu(std::uint32_t entry = 0) noexcept;
    const CpuState& state() const noexcept;
    void reset(std::uint32_t entry = 0) noexcept;
    // EE boot vector and defined reset bits in the implemented COP0 subset.
    // Other reset registers/cache/IOP initialization remain separate work.
    void reset_boot_vector() noexcept;
    // Debugger/test state import; enforces r0 and rejects unsupported Status modes.
    void restore(CpuState state);
    InstructionTrace step(Memory& memory);
    // Budget counts boundaries (including exception entries), not just retirements.
    RunResult run(Memory& memory, std::uint64_t budget,
                  std::vector<InstructionTrace>* trace = nullptr);

private:
    CpuState state_{};
};

} // namespace critterlink
