#include "critterlink/cpu.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>
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
    if ((state.cop0.status & ~0x00410c07u) != 0) {
        throw std::invalid_argument("unsupported COP0 Status mode in snapshot");
    }
    state.gpr[0] = {};
    state_ = std::move(state);
}

InstructionTrace Cpu::step(Memory& memory) {
    InstructionTrace trace{state_.pc, {}, state_.delay_slot, false, state_.stop, {}};
    if (state_.stop) {
        return trace;
    }
    // Device lines remain visible in Cause even while masked or in a handler.
    state_.cop0.cause = (state_.cop0.cause & ~0xc00u) |
                       (memory.hardware().int0() ? 0x400u : 0u) |
                       (memory.hardware().int1() ? 0x800u : 0u);
    const auto enter_exception = [&](unsigned code, std::optional<std::uint32_t> bad_address = {}) {
        auto& cop0 = state_.cop0;
        if ((cop0.status & 2u) == 0) {
            cop0.epc = state_.delay_slot ? state_.branch_pc : state_.pc;
            cop0.cause = (cop0.cause & ~0x80000000u) | (state_.delay_slot ? 0x80000000u : 0u);
        }
        cop0.cause = (cop0.cause & ~0x7cu) | (code << 2);
        if (bad_address) { cop0.bad_vaddr = *bad_address; }
        cop0.status |= 2u;
        const auto base = (cop0.status & 0x400000u) != 0 ? 0xbfc00200u : 0x80000000u;
        state_.pc = base + (code == 0 ? 0x200u : 0x180u);
        state_.next_pc = state_.pc + 4u;
        state_.delay_slot = false;
        state_.branch_pc = 0;
        trace.exception = code;
        return trace;
    };
    if ((state_.cop0.status & 0x10007u) == 0x10001u &&
        (state_.cop0.status & state_.cop0.cause & 0xc00u) != 0) {
        return enter_exception(0);
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
        if (exception) { return enter_exception(*exception, bad_address); }
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
        const bool regimm_branch = op == 1 && (rt <= 3 || (rt >= 16 && rt <= 19));
        const bool control = regimm_branch || op == 2 || op == 3 || (op >= 4 && op <= 7) ||
                             (op >= 20 && op <= 23) ||
                             (op == 0 && (function == 8 || function == 9));
        if (control && state_.delay_slot) {
            return fail(StopKind::delay_slot_branch,
                        "branch in a delay slot is unsupported; no nested branch was executed");
        }
        const auto trap_condition = [&](unsigned condition, std::uint64_t right) {
            switch (condition) {
            case 0: return !signed_less(a, right);
            case 1: return a >= right;
            case 2: return signed_less(a, right);
            case 3: return a < right;
            case 4: return a == right;
            case 6: return a != right;
            default: return false;
            }
        };
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

        const auto add_doubleword = [&](std::uint64_t left, std::uint64_t right,
                                        unsigned destination, bool trap, bool subtract) {
            const auto result = subtract ? left - right : left + right;
            const auto overflow = subtract ? ((left ^ right) & (left ^ result)) :
                                             (~(left ^ right) & (left ^ result));
            if (trap && (overflow & (std::uint64_t{1} << 63)) != 0) return false;
            write(destination, result);
            return true;
        };

        const auto hilo = [&](bool pipeline1, unsigned operation) -> std::optional<InstructionTrace> {
            auto& hi = pipeline1 ? next.hi.high : next.hi.low;
            auto& lo = pipeline1 ? next.lo.high : next.lo.low;
            if (operation >= 16 && operation <= 19) {
                const bool to_hilo = (operation & 1U) != 0;
                if (shift != 0 || rt != 0 || (to_hilo ? rd != 0 : rs != 0)) return unsupported();
                auto& selected = (operation & 2U) != 0 ? lo : hi;
                if (to_hilo) selected = a;
                else write(rd, selected);
                return {};
            }
            const bool divide = operation == 26 || operation == 27;
            if (shift != 0 || (divide && rd != 0)) return unsupported();
            if (a != sign_extend(word_a, 32) || b != sign_extend(word_b, 32)) {
                return fail(StopKind::unsupported_instruction,
                    "noncanonical word multiply/divide operands have undefined hardware results");
            }
            const bool unsigned_operation = (operation & 1U) != 0;
            const auto signed_word = [](std::uint32_t word) {
                return static_cast<std::int64_t>(word) -
                    ((word & 0x80000000U) != 0 ? 0x100000000LL : 0LL);
            };
            if (divide) {
                // Stop on unverified exceptional results; never invoke host signed division overflow.
                if (word_b == 0 || (!unsigned_operation && word_a == 0x80000000U && word_b == 0xffffffffU)) {
                    return fail(StopKind::unsupported_instruction,
                        "divide-by-zero or signed division overflow result is outside the supported policy");
                }
                const auto quotient = unsigned_operation ? std::uint64_t{word_a / word_b} :
                    static_cast<std::uint64_t>(signed_word(word_a) / signed_word(word_b));
                const auto remainder = unsigned_operation ? std::uint64_t{word_a % word_b} :
                    static_cast<std::uint64_t>(signed_word(word_a) % signed_word(word_b));
                lo = sign_extend(quotient, 32);
                hi = sign_extend(remainder, 32);
            } else {
                auto product = unsigned_operation ? std::uint64_t{word_a} * word_b :
                    static_cast<std::uint64_t>(signed_word(word_a) * signed_word(word_b));
                if (operation == 0 || operation == 1 || operation == 32 || operation == 33) {
                    product += (std::uint64_t{static_cast<std::uint32_t>(hi)} << 32U) |
                        static_cast<std::uint32_t>(lo);
                }
                lo = sign_extend(product, 32);
                hi = sign_extend(product >> 32U, 32);
                write(rd, lo);
            }
            return {};
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
            case 10: case 11:
                if (shift != 0) { return unsupported(); }
                if ((function == 10 && b == 0) || (function == 11 && b != 0)) write(rd, a);
                break;
            case 20: case 22: case 23:
            case 56: case 58: case 59: case 60: case 62: case 63: {
                const bool variable = function < 32;
                if ((!variable && rs != 0) || (variable && shift != 0)) return unsupported();
                const unsigned amount = variable ? static_cast<unsigned>(a & 63U) :
                    shift + (function >= 60 ? 32U : 0U);
                auto result = (function & 3U) == 0 ? b << amount : b >> amount;
                if ((function & 3U) == 3 && amount != 0 && (b & (std::uint64_t{1} << 63)))
                    result |= ~std::uint64_t{0} << (64U - amount);
                write(rd, result);
                break;
            }
            case 44: case 45: case 46: case 47:
                if (shift != 0) return unsupported();
                if (!add_doubleword(a, b, rd, (function & 1U) == 0, (function & 2U) != 0))
                    return fail(StopKind::exception, "signed doubleword arithmetic overflow", 12);
                break;
            case 16: case 17: case 18: case 19:
            case 24: case 25: case 26: case 27:
                if (const auto failure = hilo(false, function)) return *failure;
                break;
            case 48: case 49: case 50: case 51: case 52: case 54:
                // The ten-bit code field is software information, not a reserved field.
                if (trap_condition(function - 48U, b))
                    return fail(StopKind::exception, "register trap", 13);
                break;
            case 12: return fail(StopKind::exception, "SYSCALL", 8);
            case 13: return fail(StopKind::exception, "BREAK", 9);
            case 15:
                if (rs != 0 || rt != 0 || rd != 0) return unsupported();
                if (state_.delay_slot) {
                    return fail(StopKind::unsupported_instruction,
                                "SYNC in a branch delay slot is prohibited by the EE instruction contract");
                }
                // All 32 stypes are defined: bit 4 selects L (0) or P (1).
                // This in-order interpreter completes CPU accesses before retirement:
                // RAM is committed and FIFO stores are accepted (or the store stalls).
                // There are no pending CPU loads, write buffers or pipeline operations.
                // Thus both barriers are already satisfied; device/DMA completion is
                // not a CPU store completion requirement. Revisit when adding buffers.
                break;
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
        case 1: {
            if (rt == 8 || rt == 9 || rt == 10 || rt == 11 || rt == 12 || rt == 14) {
                // Unsigned immediate traps also sign-extend the 16-bit immediate.
                if (trap_condition(rt - 8U, signed_immediate))
                    return fail(StopKind::exception, "immediate trap", 13);
                break;
            }
            if (rt != 0 && rt != 1 && rt != 2 && rt != 3 &&
                rt != 16 && rt != 17 && rt != 18 && rt != 19) return unsupported();
            const bool link = (rt & 16U) != 0;
            if (link && rs == 31) return unsupported();
            const bool taken = (rt & 1U) ? !signed_less(a, 0) : signed_less(a, 0);
            if (link) write(31, sign_extend(pc + 8U, 32));
            if ((rt & 2U) != 0 && !taken) {
                next.pc = pc + 8U;
                next.next_pc = pc + 12U;
            } else {
                const auto offset = static_cast<std::uint32_t>(signed_immediate) * 4U;
                branch(taken ? pc + 4U + offset : pc + 8U);
            }
            break;
        }
        case 2: case 3:
            branch(((pc + 4u) & 0xf0000000u) | ((instruction & 0x03ffffffu) << 2));
            if (op == 3) { write(31, sign_extend(pc + 8u, 32)); }
            break;
        case 4: case 5: case 6: case 7:
        case 20: case 21: case 22: case 23: {
            const auto condition_op = op & 7U;
            if ((condition_op == 6 || condition_op == 7) && rt != 0) { return unsupported(); }
            const bool taken = condition_op == 4 ? a == b : condition_op == 5 ? a != b :
                               condition_op == 6 ? (a == 0 || signed_less(a, 0)) : signed_less(0, a);
            const auto offset = static_cast<std::uint32_t>(signed_immediate) * 4u;
            if (op >= 20 && !taken) {
                next.pc = pc + 8U;
                next.next_pc = pc + 12U;
            } else {
                branch(taken ? pc + 4u + offset : pc + 8u);
            }
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
        case 16:
            if (instruction == 0x42000018u) { // ERET has no delay slot.
                if (state_.delay_slot) {
                    return fail(StopKind::delay_slot_branch, "ERET in a delay slot is unsupported");
                }
                const bool error_level = (next.cop0.status & 4u) != 0;
                next.pc = error_level ? next.cop0.error_epc : next.cop0.epc;
                next.cop0.status &= ~(error_level ? 4u : 2u);
                next.next_pc = next.pc + 4u;
                break;
            }
            if ((rs != 0 && rs != 4) || (instruction & 0x7ffu) != 0) { return unsupported(); }
            if (rs == 4) {
                if (rd == 12) {
                    // IE/EXL/ERL, IM0/IM1, EIE, BEV. Other modes need their own implementation.
                    if ((word_b & ~0x00410c07u) != 0) { return unsupported(); }
                    next.cop0.status = word_b;
                } else if (rd == 14) { next.cop0.epc = word_b; }
                else if (rd == 30) { next.cop0.error_epc = word_b; }
                else { return unsupported(); }
                break;
            }
            if (rd == 8) { write(rt, sign_extend(state_.cop0.bad_vaddr, 32)); }
            else if (rd == 12) { write(rt, sign_extend(state_.cop0.status, 32)); }
            else if (rd == 13) { write(rt, sign_extend(state_.cop0.cause, 32)); }
            else if (rd == 14) { write(rt, sign_extend(state_.cop0.epc, 32)); }
            else if (rd == 30) { write(rt, sign_extend(state_.cop0.error_epc, 32)); }
            else { return unsupported(); }
            break;
        case 28:
            switch (function) {
            case 0: case 1:
                if (const auto failure = hilo(false, function)) return *failure;
                break;
            case 16: case 17: case 18: case 19:
            case 24: case 25: case 26: case 27: case 32: case 33:
                if (const auto failure = hilo(true, function)) return *failure;
                break;
            default: return unsupported();
            }
            break;
        case 24: case 25:
            if (!add_doubleword(a, signed_immediate, rt, op == 24, false))
                return fail(StopKind::exception, "signed doubleword arithmetic overflow", 12);
            break;
        case 26: case 27: case 34: case 38:
        case 42: case 44: case 45: case 46: {
            const auto address = word_a + static_cast<std::uint32_t>(signed_immediate);
            const bool load = op == 26 || op == 27 || op == 34 || op == 38;
            const bool left = op == 26 || op == 34 || op == 42 || op == 44;
            const unsigned width = op == 26 || op == 27 || op == 44 || op == 45 ? 8U : 4U;
            const auto byte = address & (width - 1U);
            const auto count = left ? byte + 1U : width - byte;
            const auto start = left ? address & ~(width - 1U) : address;
            const auto amount = left ? (width - count) * 8U : 0U;
            try {
                if (load) {
                    const auto loaded = memory.read_partial(start, count);
                    const auto mask = count == 8 ? ~std::uint64_t{0} : (std::uint64_t{1} << (count * 8U)) - 1U;
                    auto value = (b & ~(mask << amount)) | (loaded << amount);
                    // LWR preserves bits 63..32 unless the complete word is loaded.
                    if (width == 4 && (left || byte == 0)) value = sign_extend(value, 32);
                    write(rt, value);
                } else {
                    memory.write_partial(start, count, b >> amount);
                }
            } catch (const MemoryFault& fault) {
                // Byte-enabled access starts may differ from the instruction's effective address.
                throw MemoryFault(fault.reason, fault.access, address, fault.what());
            }
            break;
        }
        case 30: case 31: {
            const auto address = (word_a + static_cast<std::uint32_t>(signed_immediate)) & ~0xfU;
            if (op == 30) {
                const auto value = memory.read_quadword(address);
                if (rt != 0) next.gpr[rt] = {value[0], value[1]};
            } else {
                memory.write_quadword(address, {b, state_.gpr[rt].high});
            }
            break;
        }
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
        case 51:
            // PREF is a nonfaulting cache hint, including hint values 1..31 on EE.
            // With no cache model there is nothing to warm. Do not perform a load:
            // invalid translations are ignored and MMIO must have no read effects.
            break;
        default: return unsupported();
        }
        state_ = std::move(next);
        trace.retired = true;
        return trace;
    } catch (const MemoryStall&) {
        trace.stalled = true;
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
    std::uint64_t steps = 0;
    while (steps < budget && !state_.stop) {
        auto entry = step(memory);
        ++steps;
        if (entry.retired) { ++result.retired; }
        if (trace) { trace->push_back(std::move(entry)); }
    }
    result.budget_exhausted = !state_.stop && steps == budget;
    return result;
}

} // namespace critterlink
