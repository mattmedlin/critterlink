#include "critterlink/vector.hpp"
#include <stdexcept>
namespace critterlink {
namespace {
[[noreturn]] void unsupported(const char* message) { throw std::invalid_argument(message); }
bool unpack_format(unsigned opcode) {
    return opcode == 0x60 || opcode == 0x61 || opcode == 0x62 ||
           opcode == 0x6c || opcode == 0x6d || opcode == 0x6e;
}
unsigned element_bits(unsigned opcode) { return 32u >> (opcode & 3u); }
unsigned components(unsigned opcode) { return (opcode & 12u) == 0 ? 1u : 4u; }
unsigned unpack_destination(const VectorState& state, unsigned index) {
    return state.unpack_base + state.cycle_cl * (index / state.cycle_wl) + index % state.cycle_wl;
}
void clear_payload(VectorState& state) {
    state.payload = 0; state.payload_address = 0; state.payload_remaining = 0; state.payload_lane = 0;
    state.unpack_unsigned = false; state.unpack_base = 0; state.unpack_total = 0; state.unpack_completed = 0;
}

}
bool VectorUnit::submit_word(std::uint32_t word) {
    if (state_.payload != 0) {
        if (state_.running) unsupported("VU payload while running");
        if (state_.payload == 0x4a) {
            state_.micro[state_.payload_address][state_.payload_lane] = word;
            ++state_.payload_lane;
            --state_.payload_remaining;
            if (state_.payload_lane == 2) { state_.payload_lane = 0; ++state_.payload_address; }
            if (state_.payload_remaining == 0) clear_payload(state_);
        } else {
            const auto bits = element_bits(state_.payload);
            const auto lanes = components(state_.payload);
            const auto mask = bits == 32 ? 0xffffffffu : (std::uint32_t{1} << bits) - 1;
            for (unsigned i = 0; i < 32u / bits && state_.unpack_completed < state_.unpack_total; ++i) {
                auto value = (word >> (i * bits)) & mask;
                if (bits != 32 && !state_.unpack_unsigned && (value & (std::uint32_t{1} << (bits - 1))) != 0) {
                    value |= ~mask;
                }
                auto& destination = state_.data[state_.payload_address];
                if (lanes == 1) { destination = {value, value, value, value}; }
                else { destination[state_.payload_lane] = value; }
                if (++state_.payload_lane == lanes) {
                    state_.payload_lane = 0;
                    ++state_.unpack_completed;
                    if (state_.unpack_completed != state_.unpack_total) {
                        state_.payload_address = static_cast<std::uint16_t>(unpack_destination(state_, state_.unpack_completed));
                    }
                }
            }
            if (--state_.payload_remaining == 0) clear_payload(state_);
        }
        return true;
    }
    const auto command = word >> 24U;
    const auto immediate = word & 0xffffU;
    const auto number = (word >> 16U) & 255U;
    if (command == 0 && word == 0) return true;
    if (command == 1) {
        const auto cl = immediate & 255u, wl = immediate >> 8;
        if (cl == 0 || wl == 0 || wl > cl) unsupported("unsupported VIF cycle: only nonzero skipping cycles supported");
        state_.cycle_cl = static_cast<std::uint8_t>(cl);
        state_.cycle_wl = static_cast<std::uint8_t>(wl);
        return true;
    }
    if (command == 0x10) return !state_.running;
    if (command == 0x14 && number == 0 && immediate < 2048) {
        if (state_.running) return false;
        state_.pc = static_cast<std::uint16_t>(immediate);
        state_.running = true;
        return true;
    }
    if (command == 0x4a) {
        const auto count = number == 0 ? 256U : number;
        if (immediate >= 2048 || count > 2048 - immediate) unsupported("VIF transfer outside supported VU memory range");
        if (state_.running) return false;
        state_.payload = 0x4a;
        state_.payload_address = static_cast<std::uint16_t>(immediate);
        state_.payload_remaining = static_cast<std::uint16_t>(count * 2);
        return true;
    }
    if (unpack_format(command)) {
        const auto count = number == 0 ? 256U : number;
        if ((immediate & ~0x43ffu) != 0) unsupported("unsupported UNPACK immediate fields");
        const auto base = immediate & 0x3ffu;
        const auto last = base + state_.cycle_cl * ((count - 1) / state_.cycle_wl) + (count - 1) % state_.cycle_wl;
        if (last >= 1024) unsupported("UNPACK skipping destination outside VU memory");
        if (state_.running) return false;
        state_.payload = static_cast<std::uint8_t>(command);
        state_.payload_address = static_cast<std::uint16_t>(base);
        state_.unpack_base = static_cast<std::uint16_t>(base);
        state_.unpack_total = static_cast<std::uint16_t>(count);
        state_.unpack_unsigned = (immediate & 0x4000u) != 0;
        const auto per_word = 32u / element_bits(command);
        state_.payload_remaining = static_cast<std::uint16_t>((count * components(command) + per_word - 1) / per_word);
        return true;
    }
    unsupported("unsupported VIF1 command or command fields");
}
void VectorUnit::tick() {
    if (!state_.running) return;
    const auto lower = state_.micro[state_.pc][0];
    const auto upper = state_.micro[state_.pc][1];
    if ((upper & ~0x40000000U) != 0x000002ffU) unsupported("unsupported VU upper instruction or control bits");
    const bool ending = (upper & 0x40000000U) != 0;
    if (ending && state_.end_pending) unsupported("VU E bit in end delay slot");
    const auto opcode = lower >> 25U;
    const auto target = (lower >> 16U) & 31U;
    const auto source = (lower >> 11U) & 31U;
    if (opcode == 8) {
        if (target >= 16 || source >= 16) unsupported("unsupported VU integer register");
        const auto immediate = ((lower >> 10U) & 0x7800U) | (lower & 0x7ffU);
        if (target != 0) state_.vi[target] = static_cast<std::uint16_t>(state_.vi[source] + immediate);
    } else if (opcode == 0 || opcode == 1) {
        if (state_.end_pending) unsupported("VU load/store in E delay slot");
        const auto base = opcode == 0 ? source : target;
        const auto vector = opcode == 0 ? target : source;
        if (base >= 16) unsupported("unsupported VU address register");
        const auto raw = static_cast<std::int32_t>(lower & 0x7ffU);
        const auto offset = raw >= 1024 ? raw - 2048 : raw;
        const auto address = static_cast<std::int32_t>(state_.vi[base]) + offset;
        if (address < 0 || address >= 1024) unsupported("VU load/store outside supported memory range");
        const auto mask = (lower >> 21U) & 15U;
        for (unsigned lane = 0; lane < 4; ++lane) {
            if ((mask & (8U >> lane)) == 0) continue;
            if (opcode == 0) {
                if (vector != 0) state_.vf[vector][lane] = state_.data[static_cast<unsigned>(address)][lane];
            } else state_.data[static_cast<unsigned>(address)][lane] = state_.vf[vector][lane];
        }
    } else unsupported("unsupported VU lower instruction");
    state_.pc = static_cast<std::uint16_t>((state_.pc + 1U) & 2047U);
    if (state_.end_pending) { state_.running = false; state_.end_pending = false; }
    else state_.end_pending = ending;
}
void VectorUnit::restore(const VectorState& state) {
    if (state.pc >= 2048 || state.vi[0] != 0 || state.vf[0] != std::array<std::uint32_t,4>{0,0,0,0x3f800000}
        || (state.end_pending && !state.running)) unsupported("invalid VU execution snapshot");
    if (state.cycle_cl == 0 || state.cycle_wl == 0 || state.cycle_wl > state.cycle_cl) unsupported("invalid VIF cycle snapshot");
    const bool unpack_clear = !state.unpack_unsigned && state.unpack_base == 0 && state.unpack_total == 0 && state.unpack_completed == 0;
    if (state.payload == 0) {
        if (state.payload_address != 0 || state.payload_remaining != 0 || state.payload_lane != 0 || !unpack_clear) unsupported("invalid idle VIF snapshot");
    } else if (state.payload == 0x4a) {
        if (!unpack_clear || state.running || state.payload_address >= 2048 || state.payload_lane >= 2 || state.payload_remaining == 0 ||
            state.payload_remaining > 512 || (state.payload_remaining + state.payload_lane) % 2 != 0 ||
            state.payload_remaining > (2048u - state.payload_address) * 2 - state.payload_lane) unsupported("invalid MPG snapshot");
    } else {
        if (!unpack_format(state.payload) || state.running || state.unpack_total == 0 || state.unpack_total > 256 ||
            state.unpack_completed >= state.unpack_total || state.unpack_base >= 1024) unsupported("invalid UNPACK snapshot");
        const auto lanes = components(state.payload);
        const auto per_word = 32u / element_bits(state.payload);
        const auto done = state.unpack_completed * lanes + state.payload_lane;
        const auto total = state.unpack_total * lanes;
        if (state.payload_lane >= lanes || done % per_word != 0 ||
            state.payload_remaining != (total - done + per_word - 1) / per_word ||
            state.payload_address != unpack_destination(state, state.unpack_completed) ||
            unpack_destination(state, state.unpack_total - 1u) >= 1024) unsupported("inconsistent UNPACK progress snapshot");
    }
    state_ = state;
}
} // namespace critterlink
