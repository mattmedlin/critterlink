#pragma once

#include "critterlink/memory.hpp"

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

// Kernel diagnostic subset; see docs/interrupts.md for supported Status bits.
struct Cop0State {
    std::uint32_t bad_vaddr{};
    std::uint32_t cause{};
    std::uint32_t epc{};
    std::uint32_t status{};
    std::uint32_t error_epc{};
    bool operator==(const Cop0State&) const = default;
};

struct CpuState {
    std::array<Register128, 32> gpr{};
    Register128 hi{}, lo{};
    // Functional SA representation; preserve the complete MFSA/MTSA save token.
    std::uint64_t sa{};
    std::uint32_t pc{};
    std::uint32_t next_pc{4};
    bool delay_slot{};
    std::uint32_t branch_pc{};
    Cop0State cop0{};
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
