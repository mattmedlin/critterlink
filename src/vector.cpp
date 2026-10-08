#include "critterlink/vector.hpp"
#include <stdexcept>
namespace critterlink {
namespace {
[[noreturn]] void unsupported(const char* message) { throw std::invalid_argument(message); }
}
bool VectorUnit::submit_word(std::uint32_t word) {
    if (state_.payload != 0) {
        if (state_.running) unsupported("VU payload while running");
        const auto lanes = state_.payload == 0x4a ? 2U : 4U;
        if (state_.payload == 0x4a) state_.micro[state_.payload_address][state_.payload_lane] = word;
        else state_.data[state_.payload_address][state_.payload_lane] = word;
        ++state_.payload_lane;
        --state_.payload_remaining;
        if (state_.payload_lane == lanes) { state_.payload_lane = 0; ++state_.payload_address; }
        if (state_.payload_remaining == 0) {
            state_.payload = 0; state_.payload_address = 0; state_.payload_lane = 0;
        }
        return true;
    }
    const auto command = word >> 24U;
    const auto immediate = word & 0xffffU;
    const auto number = (word >> 16U) & 255U;
    if (command == 0 && word == 0) return true;
    if (command == 1 && word == 0x01000101U) return true;
    if (command == 0x10) return !state_.running;
    if (command == 0x14 && number == 0 && immediate < 2048) {
        if (state_.running) return false;
        state_.pc = static_cast<std::uint16_t>(immediate);
        state_.running = true;
        return true;
    }
    if (command == 0x4a || command == 0x6c) {
        const auto count = number == 0 ? 256U : number;
        const auto limit = command == 0x4a ? 2048U : 1024U;
        if (immediate >= limit || count > limit - immediate) unsupported("VIF transfer outside supported VU memory range");
        if (state_.running) return false;
        state_.payload = static_cast<std::uint8_t>(command);
        state_.payload_address = static_cast<std::uint16_t>(immediate);
        state_.payload_remaining = static_cast<std::uint16_t>(count * (command == 0x4a ? 2U : 4U));
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
    if (state.payload == 0) {
        if (state.payload_address != 0 || state.payload_remaining != 0 || state.payload_lane != 0) unsupported("invalid idle VIF snapshot");
    } else {
        if (state.payload != 0x4a && state.payload != 0x6c) unsupported("invalid VIF payload type");
        const auto lanes = state.payload == 0x4a ? 2U : 4U;
        const auto limit = state.payload == 0x4a ? 2048U : 1024U;
        if (state.running || state.payload_address >= limit || state.payload_lane >= lanes || state.payload_remaining == 0
            || state.payload_remaining > 256U * lanes || (state.payload_remaining + state.payload_lane) % lanes != 0
            || state.payload_remaining > (limit - state.payload_address) * lanes - state.payload_lane) unsupported("invalid VIF payload snapshot");
    }
    state_ = state;
}
} // namespace critterlink
