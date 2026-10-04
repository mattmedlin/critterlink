#include "critterlink/cpu.hpp"

#include <bit>
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

struct FpuArithmeticResult {
    std::uint32_t bits;
    std::uint32_t flags;
};

FpuArithmeticResult fpu_sqrt(std::uint32_t bits) {
    const auto flags = (bits & 0x80000000U) != 0 ? 0x00020040U : 0U;
    const auto exponent = (bits >> 23U) & 0xffU;
    if (exponent == 0) return {0, flags};
    const auto odd = (exponent & 1U) == 0 ? 1U : 0U;
    const auto radicand = static_cast<std::uint64_t>((bits & 0x007fffffU) | 0x00800000U) << (23U + odd);
    // Integer floor sqrt of a <=48-bit radicand; all products fit uint64.
    std::uint64_t low = 0, high = 0x01000000U;
    while (low + 1 < high) {
        const auto mid = low + (high - low) / 2;
        if (mid * mid <= radicand) low = mid;
        else high = mid;
    }
    // (low+1/2)^2 = low^2+low+1/4; an integer radicand cannot tie.
    auto root = low + (radicand - low * low > low ? 1U : 0U);
    auto result_exp = (static_cast<int>(exponent) - 127 - static_cast<int>(odd)) / 2 + 127;
    if (root >= 0x01000000U) { root >>= 1U; ++result_exp; }
    return {(static_cast<std::uint32_t>(result_exp) << 23U) |
            (static_cast<std::uint32_t>(root) & 0x007fffffU), flags};
}

FpuArithmeticResult fpu_divide(std::uint32_t left, std::uint32_t right) {
    const auto sign = (left ^ right) & 0x80000000U;
    const auto left_exp = (left >> 23U) & 0xffU;
    const auto right_exp = (right >> 23U) & 0xffU;
    if (right_exp == 0) return {sign | 0x7fffffffU, left_exp == 0 ? 0x00020040U : 0x00010020U};
    if (left_exp == 0) return {sign, 0};
    const auto numerator = (left & 0x007fffffU) | 0x00800000U;
    const auto denominator = (right & 0x007fffffU) | 0x00800000U;
    const bool renormalize = numerator < denominator;
    auto exponent = static_cast<int>(left_exp) - static_cast<int>(right_exp) + 127 - (renormalize ? 1 : 0);
    const auto scaled = static_cast<std::uint64_t>(numerator) << (renormalize ? 24U : 23U);
    auto quotient = scaled / denominator;
    const auto remainder = scaled % denominator;
    // The divide unit rounds to nearest; host rounding modes are irrelevant.
    if (remainder * 2 > denominator || (remainder * 2 == denominator && (quotient & 1U) != 0)) ++quotient;
    if (quotient >= 0x01000000U) { quotient >>= 1U; ++exponent; }
    if (exponent > 255) return {sign | 0x7fffffffU, 0};
    if (exponent <= 0) return {sign, 0};
    return {sign | (static_cast<std::uint32_t>(exponent) << 23U) |
            (static_cast<std::uint32_t>(quotient) & 0x007fffffU), 0};
}

FpuArithmeticResult fpu_add(std::uint32_t left, std::uint32_t right) {
    const auto left_exp = (left >> 23U) & 0xffU;
    const auto right_exp = (right >> 23U) & 0xffU;
    const auto exponent = left_exp > right_exp ? left_exp : right_exp;
    const auto aligned = [exponent](std::uint32_t bits, unsigned exp) -> std::int64_t {
        if (exp == 0 || exponent - exp >= 25) return 0;
        const auto magnitude = (((bits & 0x007fffffU) | 0x00800000U) << 1U) >> (exponent - exp);
        return (bits & 0x80000000U) != 0 ? -static_cast<std::int64_t>(magnitude) : magnitude;
    };
    // Retain one extra alignment bit, then normalize/truncate without host FP.
    const auto sum = aligned(left, left_exp) + aligned(right, right_exp);
    if (sum == 0) return {left & right & 0x80000000U, 0};
    const auto sign = sum < 0 ? 0x80000000U : 0U;
    auto magnitude = static_cast<std::uint32_t>(sum < 0 ? -sum : sum);
    const auto top = 31 - std::countl_zero(magnitude);
    const auto result_exp = static_cast<int>(exponent) + top - 24;
    if (result_exp > 255) return {sign | 0x7fffffffU, 0x00008010U};
    if (result_exp <= 0) return {sign, 0x00004008U};
    magnitude = top > 23 ? magnitude >> (top - 23) : magnitude << (23 - top);
    return {sign | (static_cast<std::uint32_t>(result_exp) << 23U) | (magnitude & 0x007fffffU), 0};
}

std::string hex32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return out.str();
}

// MMI0/1 arithmetic is independent in every byte, halfword or word lane.
std::optional<Register128> packed_arithmetic(unsigned function, unsigned sub,
                                            Register128 left, Register128 right) {
    if (function != 8 && function != 40) return {};
    enum class Operation { add, subtract, greater, equal, signed_add, signed_subtract,
                           unsigned_add, unsigned_subtract, minimum, maximum, absolute, mixed };
    Operation operation;
    switch (sub) {
    case 4:
        operation = function == 8 ? Operation::add : Operation::mixed; break;
    case 0: case 8:
        if (function != 8) return {};
        operation = Operation::add; break;
    case 1: case 5:
        operation = function == 8 ? Operation::subtract : Operation::absolute; break;
    case 9:
        if (function != 8) return {};
        operation = Operation::subtract; break;
    case 2: case 6: case 10:
        operation = function == 8 ? Operation::greater : Operation::equal; break;
    case 3: case 7:
        operation = function == 8 ? Operation::maximum : Operation::minimum; break;
    case 16: case 20: case 24:
        operation = function == 8 ? Operation::signed_add : Operation::unsigned_add; break;
    case 17: case 21: case 25:
        operation = function == 8 ? Operation::signed_subtract : Operation::unsigned_subtract; break;
    default: return {};
    }
    const unsigned width = 32U >> ((sub >> 2U) & 3U);
    const auto span = std::uint64_t{1} << width;
    const auto mask = span - 1U, sign = span >> 1U;
    const auto signed_lane = [&](std::uint64_t value) {
        return static_cast<std::int64_t>(value) -
               ((value & sign) != 0 ? static_cast<std::int64_t>(span) : 0);
    };
    const auto apply = [&](std::uint64_t x, std::uint64_t y, bool upper) {
        std::uint64_t output = 0;
        for (unsigned offset = 0; offset < 64; offset += width) {
            const auto a = (x >> offset) & mask, b = (y >> offset) & mask;
            std::uint64_t result = 0;
            switch (operation) {
            case Operation::add: result = a + b; break;
            case Operation::subtract: result = a - b; break;
            case Operation::greater: result = (a ^ sign) > (b ^ sign) ? mask : 0; break;
            case Operation::equal: result = a == b ? mask : 0; break;
            case Operation::minimum: result = (a ^ sign) < (b ^ sign) ? a : b; break;
            case Operation::maximum: result = (a ^ sign) > (b ^ sign) ? a : b; break;
            case Operation::absolute:
                result = b == sign ? sign - 1U : (b & sign) != 0 ? span - b : b;
                break;
            case Operation::mixed: result = upper ? a + b : a - b; break;
            case Operation::unsigned_add: result = a + b > mask ? mask : a + b; break;
            case Operation::unsigned_subtract: result = a < b ? 0 : a - b; break;
            case Operation::signed_add: case Operation::signed_subtract: {
                // At most 33 signed bits are needed, so int64 arithmetic cannot overflow.
                auto sum = operation == Operation::signed_add ? signed_lane(a) + signed_lane(b) :
                                                               signed_lane(a) - signed_lane(b);
                const auto minimum = -static_cast<std::int64_t>(sign);
                const auto maximum = static_cast<std::int64_t>(sign - 1U);
                if (sum < minimum) sum = minimum;
                if (sum > maximum) sum = maximum;
                result = static_cast<std::uint64_t>(sum);
                break;
            }
            }
            output |= (result & mask) << offset;
        }
        return output;
    };
    return Register128{apply(left.low, right.low, false), apply(left.high, right.high, true)};
}

std::optional<Register128> packed_permutation(unsigned function, unsigned sub,
                                             Register128 left, Register128 right) {
    const auto lane = [](Register128 value, unsigned index, unsigned width) {
        return ((index * width < 64 ? value.low : value.high) >> ((index * width) % 64U)) &
               ((std::uint64_t{1} << width) - 1U);
    };
    Register128 result;
    const auto put = [&](unsigned index, unsigned width, std::uint64_t value) {
        (index * width < 64 ? result.low : result.high) |= value << ((index * width) % 64U);
    };
    if ((function == 8 || function == 40) && (sub == 18 || sub == 22 || sub == 26)) {
        // PEXTL/PEXTU interleave rt,rs lanes from the selected 64-bit halves.
        const unsigned width = 32U >> ((sub >> 2U) & 3U);
        const unsigned base = function == 40 ? 64U / width : 0U;
        for (unsigned n = 0; n < 64U / width; ++n) {
            put(2 * n, width, lane(right, base + n, width));
            put(2 * n + 1, width, lane(left, base + n, width));
        }
    } else if (function == 8 && (sub == 19 || sub == 23 || sub == 27)) {
        // PPAC selects even lanes of rt into the low half, then rs into the high half.
        const unsigned width = 32U >> ((sub >> 2U) & 3U);
        for (unsigned n = 0; n < 64U / width; ++n) {
            put(n, width, lane(right, 2 * n, width));
            put(n + 64U / width, width, lane(left, 2 * n, width));
        }
    } else if ((function == 9 || function == 41) && sub == 14) {
        // PCPYUD has the opposite source order to PCPYLD.
        result = function == 9 ? Register128{right.low, left.low} : Register128{left.high, right.high};
    } else if ((function == 9 || function == 41) && sub == 10) {
        for (unsigned n = 0; n < 4; ++n) {
            put(2 * n, 16, lane(right, function == 9 ? n : 2 * n, 16));
            put(2 * n + 1, 16, lane(left, function == 9 ? n + 4 : 2 * n, 16));
        }
    } else if (function == 8 && (sub == 30 || sub == 31)) {
        // PEXT5 / PPAC5 act on four words, not a densely packed 64-bit result.
        for (unsigned n = 0; n < 4; ++n) {
            const auto word = lane(right, n, 32);
            const auto value = sub == 30 ?
                ((word & 31U) << 3U) | ((word & 0x3e0U) << 6U) |
                ((word & 0x7c00U) << 9U) | ((word & 0x8000U) << 16U) :
                ((word >> 3U) & 31U) | ((word >> 6U) & 0x3e0U) |
                ((word >> 9U) & 0x7c00U) | ((word >> 16U) & 0x8000U);
            put(n, 32, value);
        }
    } else if ((function == 9 || function == 41) && (sub == 26 || sub == 27)) {
        const std::array<unsigned, 4> order = sub == 26 ?
            (function == 9 ? std::array{2U, 1U, 0U, 3U} : std::array{0U, 2U, 1U, 3U}) :
            (function == 9 ? std::array{3U, 2U, 1U, 0U} : std::array{0U, 0U, 0U, 0U});
        for (unsigned n = 0; n < 8; ++n) put(n, 16, lane(right, (n / 4U) * 4U + order[n % 4U], 16));
    } else if ((function == 9 || function == 41) && sub == 30) {
        const auto order = function == 9 ? std::array{2U, 1U, 0U, 3U} : std::array{0U, 2U, 1U, 3U};
        for (unsigned n = 0; n < 4; ++n) put(n, 32, lane(right, order[n], 32));
    } else if (function == 9 && sub == 31) {
        constexpr std::array order{1U, 2U, 0U, 3U};
        for (unsigned n = 0; n < 4; ++n) put(n, 32, lane(right, order[n], 32));
    } else return {};
    return result;
}
} // namespace

Cpu::Cpu(std::uint32_t entry) noexcept { reset(entry); }
const CpuState& Cpu::state() const noexcept { return state_; }
void Cpu::reset(std::uint32_t entry) noexcept {
    state_ = {};
    state_.pc = entry;
    state_.next_pc = entry + 4u;
}
void Cpu::reset_boot_vector() noexcept {
    reset(0xbfc00000U);
    state_.architectural_memory = true;
    state_.cop0.status = 0x00400004U; // BEV and ERL; BEM and Cause.EXC2 clear.
}
void Cpu::restore(CpuState state) {
    if ((state.cop0.status & ~0x30410c1fu) != 0) {
        throw std::invalid_argument("unsupported COP0 Status mode in snapshot");
    }
    if ((state.fpu.control & ~0x0083c078U) != 0x01000001U) {
        throw std::invalid_argument("noncanonical FPU control register in snapshot");
    }
    if ((state.cop0.status & 0x18U) == 0x18U) throw std::invalid_argument("reserved COP0 privilege mode");
    validate_mmu(state.mmu);
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
    const auto enter_exception = [&](unsigned code, std::optional<std::uint32_t> bad_address = {}, unsigned coprocessor = 0, bool refill = false) {
        const bool use_refill = refill && (state_.cop0.status & 2U) == 0;
        auto& cop0 = state_.cop0;
        if ((cop0.status & 2u) == 0) {
            cop0.epc = state_.delay_slot ? state_.branch_pc : state_.pc;
            cop0.cause = (cop0.cause & ~0x80000000u) | (state_.delay_slot ? 0x80000000u : 0u);
        }
        cop0.cause = (cop0.cause & ~0x7cu) | (code << 2);
        if (code == 11) cop0.cause = (cop0.cause & ~0x30000000U) | (coprocessor << 28U);
        if (bad_address) { cop0.bad_vaddr = *bad_address; }
        cop0.status |= 2u;
        const auto base = (cop0.status & 0x400000u) != 0 ? 0xbfc00200u : 0x80000000u;
        state_.pc = base + (use_refill ? 0U : code == 0 ? 0x200u : 0x180u);
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

    const auto translated = [&](std::uint32_t address,unsigned alignment,Access access) {
        if(address%alignment!=0) throw TranslationFault{access==Access::store?5U:4U,address};
        Translation result{address};
        try { if(state_.architectural_memory) result=translate(state_.mmu,state_.cop0.status,address,access); }
        catch(const std::invalid_argument& error) { throw MemoryFault(MemoryError::translation,access,address,error.what()); }
        if(state_.architectural_memory && !result.scratchpad && result.address>=0x20000000U)
            throw MemoryFault(MemoryError::unmapped,access,address,"physical device range is not implemented");
        return result;
    };
    const auto read_memory = [&](std::uint32_t address,unsigned width,Access access=Access::load,bool partial=false) {
        const auto target=translated(address,partial?1U:width,access);
        try {
            return target.scratchpad?memory.read_scratchpad(target.address,width):
                partial?memory.read_partial(target.address,width):memory.read(target.address,width,access);
        } catch(MemoryFault& fault) { fault.address=address;throw; }
    };
    const auto write_memory = [&](std::uint32_t address,unsigned width,std::uint64_t value,bool partial=false) {
        const auto target=translated(address,partial?1U:width,Access::store);
        try {
            if(target.scratchpad) memory.write_scratchpad(target.address,width,value);
            else if(partial) memory.write_partial(target.address,width,value);
            else memory.write(target.address,width,value);
        } catch(MemoryFault& fault) { fault.address=address;throw; }
    };
    try {
        const auto instruction = static_cast<std::uint32_t>(read_memory(state_.pc, 4, Access::fetch));
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
                // Published PS2 results define zero-divisor outputs; wide signed
                // intermediates also safely implement INT32_MIN/-1.
                const auto quotient = word_b == 0 ?
                    ((!unsigned_operation && (word_a & 0x80000000U)) ? 1ULL : 0xffffffffULL) :
                    unsigned_operation ? std::uint64_t{word_a / word_b} :
                    static_cast<std::uint64_t>(signed_word(word_a) / signed_word(word_b));
                const auto remainder = word_b == 0 ? std::uint64_t{word_a} :
                    unsigned_operation ? std::uint64_t{word_a % word_b} :
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
            case 40: // MFSA: the saved representation is opaque to guest software.
                if (rs != 0 || rt != 0 || shift != 0) return unsupported();
                write(rd, (state_.sa / 8U) & 15U);
                break;
            case 41: // MTSA restores a previously saved representation.
                if (rt != 0 || rd != 0 || shift != 0) return unsupported();
                next.sa = (a & 15U) * 8U;
                break;
            default: return unsupported();
            }
            break;
        case 1: {
            if (rt == 24 || rt == 25) { // MTSAB / MTSAH, not REGIMM branches.
                const auto mask = rt == 24 ? 15U : 7U;
                const auto scale = rt == 24 ? 8U : 16U;
                next.sa = ((word_a ^ immediate) & mask) * scale;
                break;
            }
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
            if (!kernel_mode(state_.cop0.status) && (state_.cop0.status&0x10000000U)==0) return enter_exception(11);
            if (instruction==0x42000001U || instruction==0x42000002U || instruction==0x42000006U || instruction==0x42000008U) {
                const auto origin=translated(pc,4,Access::fetch);
                if(instruction!=0x42000008U && origin.mapped && !origin.global)
                    throw std::invalid_argument("TLBR/TLBWI/TLBWR require unmapped or global instruction space");
                if(instruction==0x42000001U) tlb_read(next.mmu);
                else if(instruction==0x42000002U) tlb_write(next.mmu,next.mmu.index&63U);
                else if(instruction==0x42000006U) tlb_write(next.mmu,next.mmu.random);
                else tlb_probe(next.mmu);
                break;
            }
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
                if (write_mmu_register(next.mmu,rd,word_b)) break;
                if (rd == 12) {
                    // IE/EXL/ERL, KSU, IM0/IM1, EIE, BEV, CU0/CU1.
                    if ((word_b & ~0x30410c1fu) != 0 || (word_b&0x18U)==0x18U) { return unsupported(); }
                    next.cop0.status = word_b;
                } else if (rd == 14) { next.cop0.epc = word_b; }
                else if (rd == 30) { next.cop0.error_epc = word_b; }
                else { return unsupported(); }
                break;
            }
            if (const auto value=read_mmu_register(state_.mmu,rd)) { write(rt,sign_extend(*value,32)); }
            else if (rd == 8) { write(rt, sign_extend(state_.cop0.bad_vaddr, 32)); }
            else if (rd == 12) { write(rt, sign_extend(state_.cop0.status, 32)); }
            else if (rd == 13) { write(rt, sign_extend(state_.cop0.cause, 32)); }
            else if (rd == 14) { write(rt, sign_extend(state_.cop0.epc, 32)); }
            else if (rd == 30) { write(rt, sign_extend(state_.cop0.error_epc, 32)); }
            else { return unsupported(); }
            break;
        case 17: {
            if ((state_.cop0.status & 0x20000000U) == 0) return enter_exception(11, {}, 1);
            if (rs == 16 && function == 4) {
                if (rd != 0) return unsupported();
                const auto result = fpu_sqrt(state_.fpu.fpr[rt]);
                next.fpu.fpr[shift] = result.bits;
                next.fpu.control = (state_.fpu.control & ~0x00030000U) | result.flags;
                break;
            }
            if (rs == 16 && function == 3) {
                const auto result = fpu_divide(state_.fpu.fpr[rd], state_.fpu.fpr[rt]);
                next.fpu.fpr[shift] = result.bits;
                next.fpu.control = (state_.fpu.control & ~0x00030000U) | result.flags;
                break;
            }
            if (rs == 16 && (function == 0 || function == 1 || function == 24 || function == 25)) {
                const bool accumulator = function >= 24;
                if (accumulator && shift != 0) return unsupported();
                const auto right = state_.fpu.fpr[rt] ^ ((function & 1U) != 0 ? 0x80000000U : 0U);
                const auto result = fpu_add(state_.fpu.fpr[rd], right);
                if (accumulator) next.fpu.accumulator = result.bits;
                else next.fpu.fpr[shift] = result.bits;
                next.fpu.control = (state_.fpu.control & ~0x0000c000U) | result.flags;
                break;
            }
            if (rs == 16 && (function == 40 || function == 41)) {
                const auto left = state_.fpu.fpr[rd];
                const auto right = state_.fpu.fpr[rt];
                // MIN/MAX select an unmodified encoding, including signed zeros
                // and exponent-zero fractions, rather than using comparison flushing.
                const auto key = [](std::uint32_t bits) {
                    return (bits & 0x80000000U) != 0 ? ~bits : bits ^ 0x80000000U;
                };
                const bool select_left = function == 40 ? key(left) >= key(right) :
                                                         key(left) <= key(right);
                next.fpu.fpr[shift] = select_left ? left : right;
                next.fpu.control &= ~0x0000c000U;
                break;
            }
            if ((rs == 16 && (function == 5 || function == 6 || function == 7 || function == 36)) ||
                (rs == 20 && function == 32)) {
                if (rt != 0) return unsupported();
                const auto bits = state_.fpu.fpr[rd];
                const auto sign = bits & 0x80000000U;
                std::uint32_t result = bits;
                if (rs == 20) { // CVT.S.W: signed word to EE float, truncate discarded bits.
                    const auto magnitude = sign != 0 ? 0U - bits : bits;
                    if (magnitude != 0) {
                        const auto exponent = 31U - static_cast<unsigned>(std::countl_zero(magnitude));
                        const auto significand = exponent > 23 ? magnitude >> (exponent - 23) :
                                                               magnitude << (23 - exponent);
                        result = sign | ((exponent + 127U) << 23U) | (significand & 0x007fffffU);
                    }
                } else if (function == 36) { // CVT.W.S: truncate toward zero, saturate overflow.
                    const auto exponent = (bits >> 23U) & 0xffU;
                    if (exponent < 127) result = 0;
                    else if (exponent > 157) result = sign != 0 ? 0x80000000U : 0x7fffffffU;
                    else {
                        const auto significand = (bits & 0x007fffffU) | 0x00800000U;
                        const auto magnitude = exponent >= 150 ? significand << (exponent - 150) :
                                                                significand >> (150 - exponent);
                        result = sign != 0 ? 0U - magnitude : magnitude;
                    }
                } else if (function != 6) {
                    result = function == 5 ? bits & 0x7fffffffU : bits ^ 0x80000000U;
                    next.fpu.control &= ~0x0000c000U; // ABS/NEG clear current O/U, not sticky flags.
                }
                next.fpu.fpr[shift] = result;
                break;
            }
            if (rs == 16 && (function == 48 || function == 50 || function == 52 || function == 54)) {
                if (shift != 0) return unsupported();
                // EE exponent-zero inputs are zero; exponent 255 is finite.
                // Integer ordering avoids importing host IEEE NaN/denormal rules.
                const auto normalized = [](std::uint32_t bits) {
                    return (bits & 0x7f800000U) == 0 ? 0U : bits;
                };
                const auto left = normalized(state_.fpu.fpr[rd]);
                const auto right = normalized(state_.fpu.fpr[rt]);
                const bool equal = left == right;
                const bool negative_left = (left & 0x80000000U) != 0;
                const bool negative_right = (right & 0x80000000U) != 0;
                const bool less = negative_left != negative_right ? negative_left :
                                  negative_left ? left > right : left < right;
                const bool condition = function == 50 ? equal : function == 52 ? less :
                                       function == 54 ? less || equal : false;
                next.fpu.control = (state_.fpu.control & ~0x00800000U) |
                                   (condition ? 0x00800000U : 0U);
                break;
            }
            if (rs == 8) {
                if (rt > 3) return unsupported();
                if (state_.delay_slot) {
                    return fail(StopKind::delay_slot_branch,
                                "COP1 branch in a delay slot is unsupported");
                }
                const bool condition = (state_.fpu.control & 0x00800000U) != 0;
                const bool taken = condition == ((rt & 1U) != 0);
                if ((rt & 2U) != 0 && !taken) {
                    next.pc = pc + 8U;
                    next.next_pc = pc + 12U;
                } else {
                    const auto offset = static_cast<std::uint32_t>(signed_immediate) * 4U;
                    branch(taken ? pc + 4U + offset : pc + 8U);
                }
                break;
            }
            if ((instruction & 0x7ffU) != 0) return unsupported();
            if (rs == 0) write(rt, sign_extend(state_.fpu.fpr[rd], 32));
            else if (rs == 4) next.fpu.fpr[rd] = word_b;
            else if (rs == 2) {
                if (rd != 0 && rd != 31) return unsupported();
                write(rt, sign_extend(rd == 0 ? 0x2e30U : state_.fpu.control, 32));
            } else if (rs == 6) {
                if (rd != 0 && rd != 31) return unsupported();
                // FCR0 is read-only; writes are ignored on the referenced hardware.
                if (rd == 31) next.fpu.control = (word_b & 0x0083c078U) | 0x01000001U;
            } else return unsupported();
            break;
        }
        case 49: case 57: { // LWC1/SWC1 transfer raw bits, including FPR0.
            if ((state_.cop0.status & 0x20000000U) == 0) return enter_exception(11, {}, 1);
            const auto address = word_a + static_cast<std::uint32_t>(signed_immediate);
            if (op == 49) next.fpu.fpr[rt] = static_cast<std::uint32_t>(read_memory(address, 4));
            else write_memory(address, 4, state_.fpu.fpr[rt]);
            break;
        }
        case 28:
            if (rs != 0 && ((function == 40 && (shift == 1 || shift == 5)) ||
                (function == 8 && (shift == 30 || shift == 31)) ||
                ((function == 9 || function == 41) && (shift == 26 || shift == 27 || shift == 30)) ||
                (function == 9 && shift == 31))) return unsupported();
            if (const auto result = packed_arithmetic(function, shift, state_.gpr[rs], state_.gpr[rt])) {
                if (rd != 0) next.gpr[rd] = *result;
                break;
            }
            if (const auto result = packed_permutation(function, shift, state_.gpr[rs], state_.gpr[rt])) {
                if (rd != 0) next.gpr[rd] = *result;
                break;
            }
            switch (function) {
            case 9: case 41: { // MMI2/MMI3.
                if (function == 9 && (shift == 16 || shift == 17 || shift == 20 || shift == 21 || shift == 28)) {
                    std::array<std::uint32_t, 8> products{};
                    for (unsigned n = 0; n < 8; ++n) {
                        const auto left = n < 4 ? a : state_.gpr[rs].high;
                        const auto right = n < 4 ? b : state_.gpr[rt].high;
                        const auto signed_half = [](std::uint64_t value) {
                            return static_cast<std::int64_t>(value & 0x7fffU) -
                                static_cast<std::int64_t>(value & 0x8000U);
                        };
                        products[n] = static_cast<std::uint32_t>(signed_half(left >> ((n % 4) * 16U)) *
                            signed_half(right >> ((n % 4) * 16U)));
                    }
                    Register128 result;
                    for (unsigned pair = 0; pair < 4; ++pair) {
                        const auto old = pair == 0 ? state_.lo.low : pair == 1 ? state_.hi.low :
                            pair == 2 ? state_.lo.high : state_.hi.high;
                        auto low = products[pair * 2], high = products[pair * 2 + 1];
                        if (shift == 16) { low += static_cast<std::uint32_t>(old); high += static_cast<std::uint32_t>(old >> 32U); }
                        else if (shift == 20) { low = static_cast<std::uint32_t>(old) - low; high = static_cast<std::uint32_t>(old >> 32U) - high; }
                        else if (shift == 17) low += high;
                        else if (shift == 21) { low = high - low; high = ~high; }
                        // Horizontal upper words follow published hardware results;
                        // the instruction manual leaves these words undefined.
                        auto& target = pair == 0 ? next.lo.low : pair == 1 ? next.hi.low :
                            pair == 2 ? next.lo.high : next.hi.high;
                        target = (std::uint64_t{high} << 32U) | low;
                        (pair < 2 ? result.low : result.high) |= std::uint64_t{low} << ((pair % 2) * 32U);
                    }
                    if (rd != 0) next.gpr[rd] = result;
                    break;
                }
                if (function == 9 && shift == 29) { // PDIVBW broadcasts the signed low halfword.
                    if (rd != 0) return unsupported();
                    const auto divisor = static_cast<std::int64_t>(b & 0x7fffU) - static_cast<std::int64_t>(b & 0x8000U);
                    Register128 quotients, remainders;
                    for (unsigned n = 0; n < 4; ++n) {
                        const auto source = n < 2 ? a : state_.gpr[rs].high;
                        const auto word = static_cast<std::uint32_t>(source >> ((n % 2) * 32U));
                        const auto dividend = static_cast<std::int64_t>(word) - ((word & 0x80000000U) ? 0x100000000LL : 0LL);
                        // Wide division handles INT32_MIN/-1 without host overflow.
                        // Zero-divisor results are corroborated by published PS2 tests.
                        const auto q = divisor == 0 ? (dividend < 0 ? 1LL : -1LL) : dividend / divisor;
                        const auto r = divisor == 0 ? dividend : dividend % divisor;
                        (n < 2 ? quotients.low : quotients.high) |= std::uint64_t{static_cast<std::uint32_t>(q)} << ((n % 2) * 32U);
                        (n < 2 ? remainders.low : remainders.high) |= std::uint64_t{static_cast<std::uint32_t>(r)} << ((n % 2) * 32U);
                    }
                    next.lo = quotients; next.hi = remainders;
                    break;
                }
                if (shift == 8 || shift == 9) { // Full-width HI/LO moves.
                    if (function == 9) {
                        if (rs != 0 || rt != 0) return unsupported();
                        if (rd != 0) next.gpr[rd] = shift == 8 ? state_.hi : state_.lo;
                    } else {
                        if (rt != 0 || rd != 0) return unsupported();
                        (shift == 8 ? next.hi : next.lo) = state_.gpr[rs];
                    }
                    break;
                }
                if ((function == 9 && shift == 2) || shift == 3) {
                    const auto variable = [&](std::uint64_t counts, std::uint64_t data) {
                        const auto count = static_cast<unsigned>(counts & 31U);
                        const auto word = static_cast<std::uint32_t>(data);
                        std::uint32_t result = shift == 2 ? word << count : word >> count;
                        if (function == 41 && count != 0 && (word & 0x80000000U) != 0)
                            result |= 0xffffffffU << (32U - count);
                        return sign_extend(result, 32);
                    };
                    if (rd != 0) next.gpr[rd] = {variable(a, b),
                        variable(state_.gpr[rs].high, state_.gpr[rt].high)};
                    break;
                }
                if (shift == 0 || (function == 9 && shift == 4) || shift == 12 || shift == 13) {
                    if (shift == 13 && rd != 0) return unsupported();
                    Register128 product;
                    for (unsigned lane = 0; lane < 2; ++lane) {
                        const auto left = lane == 0 ? a : state_.gpr[rs].high;
                        const auto right = lane == 0 ? b : state_.gpr[rt].high;
                        if (left != sign_extend(left, 32) || right != sign_extend(right, 32))
                            return fail(StopKind::unsupported_instruction,
                                        "packed word multiply/divide requires sign-extended word operands");
                        const auto x = static_cast<std::uint32_t>(left);
                        const auto y = static_cast<std::uint32_t>(right);
                        const auto signed_word = [](std::uint32_t word) {
                            return static_cast<std::int64_t>(word) -
                                ((word & 0x80000000U) != 0 ? 0x100000000LL : 0LL);
                        };
                        std::uint64_t low{}, high{};
                        if (shift != 13) {
                            auto value = function == 9 ?
                                static_cast<std::uint64_t>(signed_word(x) * signed_word(y)) :
                                static_cast<std::uint64_t>(x) * y;
                            if (shift != 12) {
                                const auto hi = lane == 0 ? state_.hi.low : state_.hi.high;
                                const auto lo = lane == 0 ? state_.lo.low : state_.lo.high;
                                const auto accumulator = (hi << 32U) | (lo & 0xffffffffULL);
                                value = shift == 4 ? accumulator - value : accumulator + value;
                            }
                            (lane == 0 ? product.low : product.high) = value;
                            low = value; high = value >> 32U;
                        } else {
                            if (y == 0) {
                                low = function == 9 && (x & 0x80000000U) ? 1U : 0xffffffffU;
                                high = x;
                            } else if (function == 9) {
                                low = static_cast<std::uint64_t>(signed_word(x) / signed_word(y));
                                high = static_cast<std::uint64_t>(signed_word(x) % signed_word(y));
                            } else { low = x / y; high = x % y; }
                        }
                        (lane == 0 ? next.lo.low : next.lo.high) = sign_extend(low, 32);
                        (lane == 0 ? next.hi.low : next.hi.high) = sign_extend(high, 32);
                    }
                    if (shift != 13 && rd != 0) next.gpr[rd] = product;
                    break;
                }
                if (shift != 18 && shift != 19) return unsupported();
                const auto logic = [&](std::uint64_t left, std::uint64_t right) {
                    if (function == 9) return shift == 18 ? left & right : left ^ right;
                    return shift == 18 ? left | right : ~(left | right);
                };
                if (rd != 0) next.gpr[rd] = {logic(a, b), logic(state_.gpr[rs].high, state_.gpr[rt].high)};
                break;
            }
            case 40: { // MMI1 QFSRV: concatenate rs above rt, then keep low 128 bits.
                if (shift != 27) return unsupported();
                if (state_.sa > 120 || (state_.sa & 7U) != 0) {
                    return fail(StopKind::unsupported_instruction,
                                "QFSRV requires an SA token generated by MTSAB/MTSAH or restored unchanged");
                }
                const std::array words{b, state_.gpr[rt].high, a, state_.gpr[rs].high};
                const auto start = static_cast<unsigned>(state_.sa / 64U);
                const auto amount = static_cast<unsigned>(state_.sa % 64U);
                const auto funnel = [&](unsigned index) {
                    if (amount == 0) return words[index];
                    return (words[index] >> amount) | (words[index + 1] << (64U - amount));
                };
                if (rd != 0) next.gpr[rd] = {funnel(start), funnel(start + 1)};
                break;
            }
            case 48: { // PMFHL: combine or saturate the accumulator words.
                if (rs != 0 || rt != 0 || shift > 4) return unsupported();
                Register128 result;
                for (unsigned lane = 0; lane < 2; ++lane) {
                    const auto hi = lane == 0 ? state_.hi.low : state_.hi.high;
                    const auto lo = lane == 0 ? state_.lo.low : state_.lo.high;
                    auto& value = lane == 0 ? result.low : result.high;
                    if (shift == 0) value = (hi << 32U) | (lo & 0xffffffffULL);
                    else if (shift == 1) value = (hi & 0xffffffff00000000ULL) | (lo >> 32U);
                    else if (shift == 2) {
                        const auto combined = (hi << 32U) | (lo & 0xffffffffULL);
                        value = signed_less(combined, 0xffffffff80000000ULL) ? 0xffffffff80000000ULL :
                            signed_less(0x7fffffffULL, combined) ? 0x7fffffffULL : sign_extend(lo, 32);
                    } else {
                        const std::array words{lo, lo >> 32U, hi, hi >> 32U};
                        for (unsigned word = 0; word < 4; ++word) {
                            auto part = words[word] & 0xffffULL;
                            if (shift == 4) {
                                const auto extended = sign_extend(words[word], 32);
                                if (signed_less(extended, 0xffffffffffff8000ULL)) part = 0x8000;
                                else if (signed_less(0x7fff, extended)) part = 0x7fff;
                            }
                            value |= part << (word * 16U);
                        }
                    }
                }
                if (rd != 0) next.gpr[rd] = result;
                break;
            }
            case 49: { // PMTHL.LW preserves the upper word of every HI/LO half.
                if (rt != 0 || rd != 0 || shift != 0) return unsupported();
                next.lo = {(state_.lo.low & 0xffffffff00000000ULL) | (a & 0xffffffffULL),
                           (state_.lo.high & 0xffffffff00000000ULL) | (state_.gpr[rs].high & 0xffffffffULL)};
                next.hi = {(state_.hi.low & 0xffffffff00000000ULL) | (a >> 32U),
                           (state_.hi.high & 0xffffffff00000000ULL) | (state_.gpr[rs].high >> 32U)};
                break;
            }
            case 52: case 54: case 55: case 60: case 62: case 63: {
                if (rs != 0) return unsupported();
                const unsigned width = function < 60 ? 16U : 32U;
                const bool left = (function & 3U) == 0;
                const bool arithmetic = (function & 3U) == 3;
                // PSLLH masks to four bits; PSRLH/PSRAH explicitly prohibit 16..31.
                if (width == 16 && !left && shift >= 16) {
                    return fail(StopKind::unsupported_instruction,
                                "halfword right shift amounts above 15 have undefined hardware results");
                }
                const auto amount = shift & (width - 1U);
                const auto mask = (std::uint64_t{1} << width) - 1U;
                const auto packed_shift = [&](std::uint64_t input) {
                    std::uint64_t output = 0;
                    for (unsigned offset = 0; offset < 64; offset += width) {
                        const auto lane = (input >> offset) & mask;
                        auto result = left ? lane << amount : lane >> amount;
                        if (arithmetic && amount != 0 && (lane & (std::uint64_t{1} << (width - 1U))))
                            result |= mask << (width - amount);
                        output |= (result & mask) << offset;
                    }
                    return output;
                };
                if (rd != 0) next.gpr[rd] = {packed_shift(b), packed_shift(state_.gpr[rt].high)};
                break;
            }
            case 4: { // PLZCW operates on the two low words, preserving bits 127..64.
                if (rt != 0 || shift != 0) return unsupported();
                const auto count = [](std::uint32_t word) {
                    const auto normalized = (word & 0x80000000U) != 0 ? ~word : word;
                    return static_cast<std::uint64_t>(std::countl_zero(normalized) - 1);
                };
                write(rd, count(word_a) | (count(static_cast<std::uint32_t>(a >> 32U)) << 32U));
                break;
            }
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
                    const auto loaded = read_memory(start, count, Access::load, true);
                    const auto mask = count == 8 ? ~std::uint64_t{0} : (std::uint64_t{1} << (count * 8U)) - 1U;
                    auto value = (b & ~(mask << amount)) | (loaded << amount);
                    // LWR preserves bits 63..32 unless the complete word is loaded.
                    if (width == 4 && (left || byte == 0)) value = sign_extend(value, 32);
                    write(rt, value);
                } else {
                    write_memory(start, count, b >> amount, true);
                }
            } catch (const TranslationFault& fault) {
                throw TranslationFault{fault.code,address,fault.refill};
            } catch (const MemoryFault& fault) {
                // Byte-enabled access starts may differ from the instruction's effective address.
                throw MemoryFault(fault.reason, fault.access, address, fault.what());
            }
            break;
        }
        case 30: case 31: {
            const auto address = (word_a + static_cast<std::uint32_t>(signed_immediate)) & ~0xfU;
            if (op == 30) {
                const auto target=translated(address,16,Access::load);
                std::array<std::uint64_t,2> value{};
                try {
                    value=target.scratchpad?std::array<std::uint64_t,2>{memory.read_scratchpad(target.address,8),memory.read_scratchpad(target.address+8,8)}:
                        memory.read_quadword(target.address);
                } catch(MemoryFault& fault) { fault.address=address;throw; }
                if (rt != 0) next.gpr[rt] = {value[0], value[1]};
            } else {
                const auto target=translated(address,16,Access::store);
                try {
                    if(target.scratchpad) {
                        memory.write_scratchpad(target.address,8,b);
                        memory.write_scratchpad(target.address+8,8,state_.gpr[rt].high);
                    } else memory.write_quadword(target.address,{b,state_.gpr[rt].high});
                } catch(MemoryFault& fault) { fault.address=address;throw; }
            }
            break;
        }
        case 32: case 33: case 35: case 36: case 37: case 39: case 55: {
            const auto address = word_a + static_cast<std::uint32_t>(signed_immediate);
            const unsigned width = op == 32 || op == 36 ? 1 : op == 33 || op == 37 ? 2 : op == 55 ? 8 : 4;
            auto value = read_memory(address, width);
            if (op == 32 || op == 33 || op == 35) { value = sign_extend(value, width * 8); }
            write(rt, value);
            break;
        }
        case 40: case 41: case 43: case 63: {
            const auto address = word_a + static_cast<std::uint32_t>(signed_immediate);
            const unsigned width = op == 40 ? 1 : op == 41 ? 2 : op == 43 ? 4 : 8;
            write_memory(address, width, b);
            break;
        }
        case 51:
            // PREF is a nonfaulting cache hint, including hint values 1..31 on EE.
            // With no cache model there is nothing to warm. Do not perform a load:
            // invalid translations are ignored and MMIO must have no read effects.
            break;
        default: return unsupported();
        }
        if(next.architectural_memory && !(op==16 && rs==4 && rd==6)) advance_random(next.mmu);
        state_ = std::move(next);
        trace.retired = true;
        return trace;
    } catch (const TranslationFault& fault) {
        if(fault.code<=3) {
            state_.mmu.context=(state_.mmu.context&0xff800000U)|((fault.address>>9U)&0x007ffff0U);
            state_.mmu.hi=(fault.address&0xffffe000U)|(state_.mmu.hi&0xffU);
        }
        return enter_exception(fault.code,fault.address,0,fault.refill);
    } catch (const std::invalid_argument& error) {
        return fail(StopKind::unsupported_instruction,error.what());
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
