#include "critterlink/memory.hpp"

#include <algorithm>
#include <utility>

namespace critterlink {

MemoryFault::MemoryFault(MemoryError reason_value, Access access_value, std::uint32_t address_value, const std::string& detail)
    : std::runtime_error(!detail.empty() ? detail : reason_value == MemoryError::alignment ? "unaligned memory access" :
                         reason_value == MemoryError::translation ? "TLB translation is not implemented" :
                         "address has no RAM or implemented device"),
      reason(reason_value), access(access_value), address(address_value) {}

Memory::Memory() : ram_(ram_size) {}

void Memory::load_boot_rom(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > boot_rom_max_size) {
        throw std::invalid_argument("boot ROM must contain 1 through 4194304 bytes");
    }
    std::vector<std::uint8_t> replacement(bytes.begin(), bytes.end());
    boot_rom_ = std::move(replacement);
}

Memory::Region Memory::resolve(std::uint32_t address, unsigned width, Access access) const {
    if (width != 1 && width != 2 && width != 4 && width != 8 && width != 16) {
        throw std::invalid_argument("invalid aligned RAM width");
    }
    if (address % width != 0) {
        throw MemoryFault(MemoryError::alignment, access, address);
    }
    return resolve_range(address, width, access);
}

Memory::Region Memory::resolve_range(std::uint32_t address, unsigned count, Access access) const {
    auto physical = address;
    const bool direct = address >= 0x80000000u && address < 0xc0000000u;
    if (direct) {
        physical = address & 0x1fffffffu;
    }
    if ((physical >= 0x10000000u && physical < 0x10010000u) ||
        (physical >= 0x12000000u && physical < 0x14000000u)) {
        throw MemoryFault(MemoryError::device, access, address,
                          "this partial or quadword MMIO access is not implemented");
    }
    if (physical >= boot_rom_base && physical < boot_rom_base + boot_rom_max_size) {
        const auto offset = physical - boot_rom_base;
        if (offset >= boot_rom_.size() || count > boot_rom_.size() - offset) {
            throw MemoryFault(MemoryError::unmapped, access, address, "boot ROM bytes are not loaded for this range");
        }
        if (access == Access::store) {
            throw MemoryFault(MemoryError::device, access, address, "boot ROM writes are unsupported");
        }
        return {true, offset};
    }
    if (!direct && address >= ram_size) {
        throw MemoryFault(MemoryError::translation, access, address);
    }
    if (physical >= ram_size || count > ram_size - physical) {
        throw MemoryFault(MemoryError::unmapped, access, address);
    }
    return {false, physical};
}

std::uint64_t Memory::read_partial(std::uint32_t address, unsigned count) const {
    if (count == 0 || count > 8) { throw std::invalid_argument("partial access requires 1 to 8 bytes"); }
    const auto region = resolve_range(address, count, Access::load);
    std::uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= std::uint64_t{(region.rom ? boot_rom_ : ram_)[region.offset + i]} << (i * 8);
    return value;
}

void Memory::write_partial(std::uint32_t address, unsigned count, std::uint64_t value) {
    if (count == 0 || count > 8) { throw std::invalid_argument("partial access requires 1 to 8 bytes"); }
    const auto region = resolve_range(address, count, Access::store);
    for (unsigned i = 0; i < count; ++i) ram_[region.offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

std::array<std::uint64_t, 2> Memory::read_quadword(std::uint32_t address) const {
    const auto region = resolve(address, 16, Access::load);
    std::array<std::uint64_t, 2> value{};
    for (unsigned i = 0; i < 16; ++i) value[i / 8] |= std::uint64_t{(region.rom ? boot_rom_ : ram_)[region.offset + i]} << ((i % 8) * 8);
    return value;
}

void Memory::write_quadword(std::uint32_t address, const std::array<std::uint64_t, 2>& value) {
    if ((address & 15U) != 0) { throw MemoryFault(MemoryError::alignment, Access::store, address); }
    const auto physical = address >= 0x80000000U && address < 0xc0000000U ? address & 0x1fffffffU : address;
    if (physical >= 0x10000000U && physical < 0x10010000U) {
        const std::array<std::uint32_t, 4> words{static_cast<std::uint32_t>(value[0]),
            static_cast<std::uint32_t>(value[0] >> 32U), static_cast<std::uint32_t>(value[1]),
            static_cast<std::uint32_t>(value[1] >> 32U)};
        try {
            if (!hardware_.write_quadword(physical, words)) throw MemoryStall{};
        } catch (const std::invalid_argument& error) {
            throw MemoryFault(MemoryError::device, Access::store, address, error.what());
        }
        return;
    }
    const auto region = resolve(address, 16, Access::store);
    for (unsigned i = 0; i < 16; ++i) ram_[region.offset + i] = static_cast<std::uint8_t>(value[i / 8] >> ((i % 8) * 8));
}

std::uint64_t Memory::read(std::uint32_t address, unsigned width, Access access) const {
    if (width != 1 && width != 2 && width != 4 && width != 8) { throw std::invalid_argument("invalid memory width"); }
    if (address % width != 0) { throw MemoryFault(MemoryError::alignment, access, address); }
    const auto physical = address >= 0x80000000u && address < 0xc0000000u ? address & 0x1fffffffu : address;
    if (physical >= 0x12000000u && physical < 0x14000000u) {
        if (width != 8 || access == Access::fetch) {
            throw MemoryFault(MemoryError::device, access, address, "GS privileged ports require 64-bit data access");
        }
        try { return hardware_.read_gs(physical); }
        catch (const std::invalid_argument& error) { throw MemoryFault(MemoryError::device, access, address, error.what()); }
    }
    if (physical >= 0x10000000 && physical < 0x10010000) {
        if (width != 4 || access == Access::fetch || address % 4 != 0) {
            throw MemoryFault(MemoryError::device, access, address, "EE MMIO requires aligned 32-bit data access");
        }
        try { return hardware_.read(physical); }
        catch (const std::invalid_argument& error) { throw MemoryFault(MemoryError::device, access, address, error.what()); }
    }
    const auto region = resolve(address, width, access);
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) {
        value |= std::uint64_t{(region.rom ? boot_rom_ : ram_)[region.offset + i]} << (i * 8);
    }
    return value;
}

void Memory::write(std::uint32_t address, unsigned width, std::uint64_t value) {
    if (width != 1 && width != 2 && width != 4 && width != 8) { throw std::invalid_argument("invalid memory width"); }
    if (address % width != 0) { throw MemoryFault(MemoryError::alignment, Access::store, address); }
    const auto physical = address >= 0x80000000u && address < 0xc0000000u ? address & 0x1fffffffu : address;
    if (physical >= 0x12000000u && physical < 0x14000000u) {
        if (width != 8) {
            throw MemoryFault(MemoryError::device, Access::store, address, "GS privileged ports require 64-bit data access");
        }
        try { hardware_.write_gs(physical, value); }
        catch (const std::invalid_argument& error) { throw MemoryFault(MemoryError::device, Access::store, address, error.what()); }
        return;
    }
    if (physical >= 0x10000000 && physical < 0x10010000) {
        if (width != 4 || address % 4 != 0) {
            throw MemoryFault(MemoryError::device, Access::store, address, "EE MMIO requires aligned 32-bit data access");
        }
        try { hardware_.write(physical, static_cast<std::uint32_t>(value)); }
        catch (const std::invalid_argument& error) { throw MemoryFault(MemoryError::device, Access::store, address, error.what()); }
        return;
    }
    const auto region = resolve(address, width, Access::store);
    for (unsigned i = 0; i < width; ++i) {
        ram_[region.offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
    }
}

std::uint64_t Memory::read_scratchpad(std::uint32_t offset,unsigned count) const {
    if(count==0 || count>8 || offset>=scratchpad_.size() || count>scratchpad_.size()-offset)
        throw std::invalid_argument("scratchpad byte range is outside 16 KiB");
    std::uint64_t result=0;
    for(unsigned n=0;n<count;++n) result|=std::uint64_t{scratchpad_[offset+n]}<<(8*n);
    return result;
}
void Memory::write_scratchpad(std::uint32_t offset,unsigned count,std::uint64_t value) {
    if(count==0 || count>8 || offset>=scratchpad_.size() || count>scratchpad_.size()-offset)
        throw std::invalid_argument("scratchpad byte range is outside 16 KiB");
    for(unsigned n=0;n<count;++n) scratchpad_[offset+n]=static_cast<std::uint8_t>(value>>(8*n));
}

void Memory::clear() noexcept { std::fill(ram_.begin(), ram_.end(), std::uint8_t{0}); }
std::span<const std::uint8_t> Memory::bytes() const noexcept { return ram_; }

MemoryState Memory::state() const { return {ram_, hardware_.state(), boot_rom_, scratchpad_}; }
void Memory::restore(const MemoryState& state) {
    if (state.ram.size() != ram_size) { throw std::invalid_argument("snapshot RAM must contain exactly 32 MiB"); }
    if (state.boot_rom.size() > boot_rom_max_size) { throw std::invalid_argument("snapshot boot ROM exceeds 4 MiB"); }
    if(state.scratchpad.size()!=16384) throw std::invalid_argument("snapshot scratchpad must contain exactly 16 KiB");
    auto replacement_scratchpad = state.scratchpad;
    auto replacement_rom = state.boot_rom;
    auto replacement_ram = state.ram;
    Hardware replacement_hardware = hardware_;
    replacement_hardware.restore(state.hardware);
    scratchpad_ = std::move(replacement_scratchpad);
    boot_rom_ = std::move(replacement_rom);
    ram_ = std::move(replacement_ram);
    hardware_ = std::move(replacement_hardware);
}

} // namespace critterlink
