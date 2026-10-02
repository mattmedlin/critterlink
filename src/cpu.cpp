#include "critterlink/cpu.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace critterlink {
namespace {
std::uint64_t sign_extend(std::uint64_t value, unsigned bits) {
    const auto sign = std::uint64_t{1} << (bits - 1);
    const auto mask = (std::uint64_t{1} << bits) - 1;
    value &= mask;
    return (value ^ sign) - sign;
}

bool signed_less(std::uint64_t a, std::uint64_t b) {
    constexpr auto sign = std::uint64_t{1} << 63;
    return (a ^ sign) < (b ^ sign);
}

std::string hex32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return out.str();
}
} // namespace

Cpu::Cpu(std::uint32_t entry) noexcept { reset(entry); }
const CpuState& Cpu::state() const noexcept { return state_; }
void Cpu::reset(std::uint32_t entry) noexcept {
    state_ = {};
    state_.pc = entry;
    state_.next_pc = entry + 4u;
}
void Cpu::restore(CpuState state) {
    state.gpr[0] = {};
    state_ = std::move(state);
}

InstructionTrace Cpu::step(Memory& memory) {
    InstructionTrace trace{state_.pc, {}, state_.delay_slot, false, state_.stop};
    if (state_.stop) {
        return trace;
    }
    const auto fail = [&](StopKind kind, const std::string& reason,
                          std::optional<unsigned> exception = {},
                          std::optional<std::uint32_t> bad_address = {}) {
        std::string message = "pc=" + hex32(state_.pc);
        message += trace.instruction ? " opcode=" + hex32(*trace.instruction) : " fetch";
        if (state_.delay_slot) {
            message += " delay-slot-of=" + hex32(state_.branch_pc);
        }
        message += ": " + reason;
        if (exception) {
            state_.cop0.cause = (*exception << 2) | (state_.delay_slot ? 0x80000000u : 0u);
            state_.cop0.epc = state_.delay_slot ? state_.branch_pc : state_.pc;
            if (bad_address) {
                state_.cop0.bad_vaddr = *bad_address;
            }
        }
        state_.stop = CpuStop{kind, state_.pc, trace.instruction, message};
        trace.stop = state_.stop;
        return trace;
    };
    const auto unsupported = [&] {
        return fail(StopKind::unsupported_instruction,
                    "unsupported instruction or encoding; see docs/cpu-coverage.md");
    };

    try {
        const auto instruction = static_cast<std::uint32_t>(memory.read(state_.pc, 4, Access::fetch));
        trace.instruction = instruction;
        const auto op = instruction >> 26;
        const auto rs = (instruction >> 21) & 31;
        const auto rt = (instruction >> 16) & 31;
        const auto rd = (instruction >> 11) & 31;
        const auto shift = (instruction >> 6) & 31;
        const auto function = instruction & 63;
        const auto immediate = instruction & 0xffff;
        const auto signed_immediate = sign_extend(immediate, 16);
        const auto a = state_.gpr[rs].low;
        const auto b = state_.gpr[rt].low;
        const auto word_a = static_cast<std::uint32_t>(a);
        const auto word_b = static_cast<std::uint32_t>(b);
        const auto pc = state_.pc;
        auto next = state_;
        next.pc = state_.next_pc;
        next.next_pc = state_.next_pc + 4u;
        next.delay_slot = false;
        next.branch_pc = 0;
        const auto write = [&](unsigned index, std::uint64_t value) {
            if (index != 0) {
                next.gpr[index].low = value;
            }
        };
        const auto branch = [&](std::uint32_t target) {
            next.next_pc = target;
            next.delay_slot = true;
            next.branch_pc = pc;
        };
        const bool control = op == 2 || op == 3 || (op >= 4 && op <= 7) ||
                             (op == 0 && (function == 8 || function == 9));
        if (control && state_.delay_slot) {
            return fail(StopKind::delay_slot_branch,
                        "branch in a delay slot is unsupported; no nested branch was executed");
        }
        const auto add_word = [&](std::uint32_t left, std::uint32_t right,
                                  unsigned destination, bool trap, bool subtract) {
            const std::uint32_t result = subtract ? left - right : left + right;
            const auto overflow = subtract ? ((left ^ right) & (left ^ result)) :
                                             (~(left ^ right) & (left ^ result));
            if (trap && (overflow & 0x80000000u) != 0) {
                return false;
            }
            write(destination, sign_extend(result, 32));
            return true;
        };

        switch (op) {
        case 0:
            switch (function) {
            case 0: case 2: case 3: case 4: case 6: case 7: {
                const bool variable = (function & 4u) != 0;
                if ((!variable && rs != 0) || (variable && shift != 0)) {
                    return unsupported();
                }
                const auto amount = variable ? (word_a & 31u) : shift;
                std::uint32_t result = 0;
                if ((function & 3u) == 0) {
                    result = word_b << amount;
                } else {
                    result = word_b >> amount;
                    if ((function & 3u) == 3 && amount != 0 && (word_b & 0x80000000u)) {
                        result |= 0xffffffffu << (32u - amount);
                    }
                }
                write(rd, sign_extend(result, 32));
                break;
            }
            case 8: case 9:
                if (rt != 0 || shift != 0 || (function == 8 && rd != 0) ||
                    (function == 9 && rd == rs)) {
                    return unsupported();
                }
                branch(static_cast<std::uint32_t>(a));
                if (function == 9) {
                    write(rd, sign_extend(pc + 8u, 32));
                }
                break;
            case 12: return fail(StopKind::exception, "SYSCALL; exception dispatch is not implemented", 8);
            case 13: return fail(StopKind::exception, "BREAK; exception dispatch is not implemented", 9);
            case 32: case 33: case 34: case 35:
                if (shift != 0) { return unsupported(); }
                if (!add_word(word_a, word_b, rd, (function & 1u) == 0, (function & 2u) != 0)) {
                    return fail(StopKind::exception, "signed word arithmetic overflow", 12);
                }
                break;
            case 36: case 37: case 38: case 39: case 42: case 43:
                if (shift != 0) { return unsupported(); }
                if (function == 36) { write(rd, a & b); }
                if (function == 37) { write(rd, a | b); }
                if (function == 38) { write(rd, a ^ b); }
                if (function == 39) { write(rd, ~(a | b)); }
                if (function == 42) { write(rd, signed_less(a, b) ? 1 : 0); }
                if (function == 43) { write(rd, a < b ? 1 : 0); }
                break;
            default: return unsupported();
            }
            break;
        case 2: case 3:
            branch(((pc + 4u) & 0xf0000000u) | ((instruction & 0x03ffffffu) << 2));
            if (op == 3) { write(31, sign_extend(pc + 8u, 32)); }
            break;
        case 4: case 5: case 6: case 7: {
            if ((op == 6 || op == 7) && rt != 0) { return unsupported(); }
            const bool taken = op == 4 ? a == b : op == 5 ? a != b :
                               op == 6 ? (a == 0 || signed_less(a, 0)) : signed_less(0, a);
            const auto offset = static_cast<std::uint32_t>(signed_immediate) * 4u;
            branch(taken ? pc + 4u + offset : pc + 8u);
            break;
        }
        case 8: case 9:
            if (!add_word(word_a, static_cast<std::uint32_t>(signed_immediate), rt, op == 8, false)) {
                return fail(StopKind::exception, "signed word arithmetic overflow", 12);
            }
            break;
        case 10: write(rt, signed_less(a, signed_immediate) ? 1 : 0); break;
        case 11: write(rt, a < signed_immediate ? 1 : 0); break;
        case 12: write(rt, a & immediate); break;
        case 13: write(rt, a | immediate); break;
        case 14: write(rt, a ^ immediate); break;
        case 15:
            if (rs != 0) { return unsupported(); }
            write(rt, sign_extend(immediate << 16, 32));
            break;
        case 16: // Read-only subset of MFC0; all other COP0 operations stop.
            if (rs != 0 || (instruction & 0x7ffu) != 0) { return unsupported(); }
            if (rd == 8) { write(rt, sign_extend(state_.cop0.bad_vaddr, 32)); }
            else if (rd == 13) { write(rt, sign_extend(state_.cop0.cause, 32)); }
            else if (rd == 14) { write(rt, sign_extend(state_.cop0.epc, 32)); }
            else { return unsupported(); }
            break;
        case 32: case 33: case 35: case 36: case 37: case 39: case 55: {
            const auto address = word_a + static_cast<std::uint32_t>(signed_immediate);
            const unsigned width = op == 32 || op == 36 ? 1 : op == 33 || op == 37 ? 2 : op == 55 ? 8 : 4;
            auto value = memory.read(address, width);
            if (op == 32 || op == 33 || op == 35) { value = sign_extend(value, width * 8); }
            write(rt, value);
            break;
        }
        case 40: case 41: case 43: case 63: {
            const auto address = word_a + static_cast<std::uint32_t>(signed_immediate);
            const unsigned width = op == 40 ? 1 : op == 41 ? 2 : op == 43 ? 4 : 8;
            memory.write(address, width, b);
            break;
        }
        default: return unsupported();
        }
        state_ = std::move(next);
        trace.retired = true;
        return trace;
    } catch (const MemoryFault& fault) {
        const auto access = fault.access == Access::fetch ? "fetch" : fault.access == Access::load ? "load" : "store";
        const auto reason = std::string(access) + " address=" + hex32(fault.address) + ": " + fault.what();
        if (fault.reason == MemoryError::alignment) {
            return fail(StopKind::exception, reason, fault.access == Access::store ? 5u : 4u, fault.address);
        }
        // Unsupported translation/devices are emulator limitations, not fake TLB exceptions.
        return fail(StopKind::unsupported_access, reason);
    }
}

RunResult Cpu::run(Memory& memory, std::uint64_t budget, std::vector<InstructionTrace>* trace) {
    RunResult result;
    while (result.retired < budget && !state_.stop) {
        auto entry = step(memory);
        if (entry.retired) { ++result.retired; }
        if (trace) { trace->push_back(std::move(entry)); }
    }
    result.budget_exhausted = !state_.stop && result.retired == budget;
    return result;
}

} // namespace critterlink
