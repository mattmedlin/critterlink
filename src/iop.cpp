#include "critterlink/iop.hpp"

#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <utility>

namespace critterlink {
namespace {
std::uint32_t physical(std::uint32_t address) {
    if (address >= 0x80000000U && address < 0xc0000000U) return address & 0x1fffffffU;
    return address;
}
std::uint32_t aligned(std::uint32_t address, unsigned width = 4) {
    if ((address & (width - 1U)) != 0) throw std::invalid_argument("misaligned IOP data access");
    return physical(address);
}
std::uint32_t offset(std::uint32_t instruction) {
    const auto immediate = instruction & 0xffffU;
    return (immediate & 0x8000U) ? immediate | 0xffff0000U : immediate;
}
}

std::uint16_t IopBus::read16(std::uint32_t) {
    throw std::invalid_argument("unsupported IOP halfword MMIO read");
}
void IopBus::write16(std::uint32_t, std::uint16_t) {
    throw std::invalid_argument("unsupported IOP halfword MMIO write");
}
std::uint16_t Iop::read16(std::uint32_t address) const {
    const auto p = aligned(address, 2);
    if (p >= ram_size) throw std::invalid_argument("unsupported IOP RAM read");
    return static_cast<std::uint16_t>(state_.ram[p] | (std::uint32_t{state_.ram[p + 1]} << 8U));
}
void Iop::write16(std::uint32_t address, std::uint16_t value) {
    const auto p = aligned(address, 2);
    if (p >= ram_size) throw std::invalid_argument("unsupported IOP RAM write");
    state_.ram[p] = static_cast<std::uint8_t>(value);
    state_.ram[p + 1] = static_cast<std::uint8_t>(value >> 8U);
}
const IopState& Iop::state() const noexcept { return state_; }
std::span<std::uint8_t> Iop::mutable_ram() noexcept { return state_.ram; }
void Iop::restore(IopState state) {
    if (state.ram.size() != ram_size || state.gpr[0] != 0 ||
        (state.pc & 3U) != 0 || (state.next_pc & 3U) != 0 ||
        (!state.delay_slot && state.next_pc != state.pc + 4U) ||
        (state.pending_load && (state.pending_load->reg == 0 || state.pending_load->reg >= 32)) ||
        (state.stop && state.stop->empty()) ||
        (!state.enabled && (state.stop || state.pending_load || state.delay_slot))) {
        throw std::invalid_argument("invalid IOP snapshot");
    }
    state_ = std::move(state);
}
void Iop::start(std::uint32_t entry) {
    if (aligned(entry) >= ram_size) throw std::invalid_argument("IOP entry is outside RAM");
    state_.gpr = {};
    state_.pc = entry;
    state_.next_pc = entry + 4U;
    state_.delay_slot = false;
    state_.pending_load.reset();
    state_.stop.reset();
    state_.enabled = true;
}
std::uint32_t Iop::read32(std::uint32_t address) const {
    const auto p = aligned(address);
    if (p >= ram_size) throw std::invalid_argument("unsupported IOP RAM read");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{state_.ram[p + i]} << (i * 8U);
    return value;
}
void Iop::write32(std::uint32_t address, std::uint32_t value) {
    const auto p = aligned(address);
    if (p >= ram_size) throw std::invalid_argument("unsupported IOP RAM write");
    for (unsigned i = 0; i < 4; ++i) state_.ram[p + i] = static_cast<std::uint8_t>(value >> (i * 8U));
}

bool Iop::step(IopBus* bus) {
    if (!state_.enabled || state_.stop) return false;
    std::optional<std::uint32_t> opcode;
    try {
        const auto instruction = read32(state_.pc);
        opcode = instruction;
        const auto op = instruction >> 26U;
        const auto rs = (instruction >> 21U) & 31U;
        const auto rt = (instruction >> 16U) & 31U;
        const auto rd = (instruction >> 11U) & 31U;
        const auto shift = (instruction >> 6U) & 31U;
        const auto fn = instruction & 63U;
        const auto left = state_.gpr[rs];
        const auto right = state_.gpr[rt];
        std::optional<IopLoad> write;
        std::optional<IopLoad> load;
        bool branch = false;
        auto following = state_.next_pc + 4U;
        auto set_branch = [&](std::uint32_t destination) {
            if (state_.delay_slot) throw std::invalid_argument("IOP branch in delay slot unsupported");
            branch = true;
            following = destination;
        };
        switch (op) {
        case 0:
            if (fn == 0 && rs == 0) write = IopLoad{rd, right << shift};
            else if (fn == 0x21 && shift == 0) write = IopLoad{rd, left + right};
            else if (fn == 8 && rt == 0 && rd == 0 && shift == 0) {
                if ((left & 3U) != 0) throw std::invalid_argument("misaligned IOP jump target");
                set_branch(left);
            } else throw std::invalid_argument("unsupported IOP SPECIAL instruction");
            break;
        case 2: set_branch(((state_.pc + 4U) & 0xf0000000U) | ((instruction & 0x03ffffffU) << 2U)); break;
        case 4:
        case 5: {
            const bool taken = (op == 4) ? left == right : left != right;
            set_branch(taken ? state_.pc + 4U + (offset(instruction) << 2U) : state_.pc + 8U);
            break;
        }
        case 9: write = IopLoad{rt, left + offset(instruction)}; break;
        case 12: write = IopLoad{rt, left & (instruction & 0xffffU)}; break;
        case 13: write = IopLoad{rt, left | (instruction & 0xffffU)}; break;
        case 15:
            if (rs != 0) throw std::invalid_argument("unsupported IOP LUI encoding");
            write = IopLoad{rt, instruction << 16U}; break;
        case 33:
        case 37:
        case 35: {
            if (state_.pending_load && state_.pending_load->reg == rt)
                throw std::invalid_argument("overlapping IOP loads to the same register unsupported");
            const auto address = aligned(left + offset(instruction), op == 35 ? 4 : 2);
            if (address >= ram_size && !bus) throw std::invalid_argument("unsupported IOP MMIO read");
            auto value = op == 35
                ? (address < ram_size ? read32(address) : bus->read32(address))
                : std::uint32_t{address < ram_size ? read16(address) : bus->read16(address)};
            if (op == 33 && (value & 0x8000U) != 0) value |= 0xffff0000U;
            if (rt != 0) load = IopLoad{rt, value};
            break;
        }
        case 41: {
            const auto address = aligned(left + offset(instruction), 2);
            if (address < ram_size) write16(address, static_cast<std::uint16_t>(right));
            else if (bus) bus->write16(address, static_cast<std::uint16_t>(right));
            else throw std::invalid_argument("unsupported IOP halfword MMIO write");
            break;
        }
        case 43: {
            const auto address = aligned(left + offset(instruction));
            if (address < ram_size) write32(address, right);
            else if (bus) bus->write32(address, right);
            else throw std::invalid_argument("unsupported IOP MMIO write");
            break;
        }
        default: throw std::invalid_argument("unsupported IOP instruction");
        }
        // Operand reads precede older load writeback. A younger ALU write wins.
        if (state_.pending_load) state_.gpr[state_.pending_load->reg] = state_.pending_load->value;
        if (write && write->reg != 0) state_.gpr[write->reg] = write->value;
        state_.pending_load = load;
        state_.pc = state_.next_pc;
        state_.next_pc = following;
        state_.delay_slot = branch;
        state_.gpr[0] = 0;
        return true;
    } catch (const std::exception& error) {
        std::ostringstream diagnostic;
        diagnostic << "pc=0x" << std::hex << std::setfill('0') << std::setw(8) << state_.pc;
        if (opcode) { diagnostic << " opcode=0x" << std::setw(8) << *opcode; }
        else { diagnostic << " fetch"; }
        if (state_.delay_slot) { diagnostic << " delay-slot"; }
        diagnostic << ": " << error.what();
        state_.stop = diagnostic.str();
        return false;
    }
}
} // namespace critterlink
