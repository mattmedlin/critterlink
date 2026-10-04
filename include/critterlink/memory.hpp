#pragma once

#include "critterlink/hardware.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include <stdexcept>
#include <vector>

namespace critterlink {

enum class Access { fetch, load, store };
enum class MemoryError { alignment, unmapped, translation, device };

struct MemoryFault : std::runtime_error {
    MemoryFault(MemoryError reason, Access access, std::uint32_t address, const std::string& detail = {});
    MemoryError reason;
    Access access;
    std::uint32_t address;
};
struct MemoryStall : std::runtime_error {
    MemoryStall() : std::runtime_error("GIF FIFO is full; retry after device progress") {}
};

struct MemoryState {
    std::vector<std::uint8_t> ram;
    HardwareState hardware;
    std::vector<std::uint8_t> boot_rom;
    bool operator==(const MemoryState&) const = default;
};

// Bootstrap RAM, boot ROM and supported EE MMIO. No TLB/cache implementation.
class Memory {
public:
    static constexpr std::size_t ram_size = 32 * 1024 * 1024;
    static constexpr std::uint32_t boot_rom_base = 0x1fc00000U;
    static constexpr std::size_t boot_rom_max_size = 4 * 1024 * 1024;
    Memory();
    // Copies 1 byte through 4 MiB of caller-owned bytes; does not reset CPU, RAM or devices.
    // Smaller images occupy only their supplied range; no invented padding.
    void load_boot_rom(std::span<const std::uint8_t> bytes);
    std::span<const std::uint8_t> boot_rom_bytes() const noexcept { return boot_rom_; }
    std::uint64_t read(std::uint32_t address, unsigned width, Access access = Access::load) const;
    void write(std::uint32_t address, unsigned width, std::uint64_t value);
    // Byte-enabled memory accesses; validate the entire range before writing.
    std::uint64_t read_partial(std::uint32_t address, unsigned count) const;
    void write_partial(std::uint32_t address, unsigned count, std::uint64_t value);
    // Aligned RAM accesses and GIF FIFO writes; a full FIFO throws MemoryStall.
    // EE LQ/SQ mask the effective address in the CPU.
    std::array<std::uint64_t, 2> read_quadword(std::uint32_t address) const;
    void write_quadword(std::uint32_t address, const std::array<std::uint64_t, 2>& value);
    void clear() noexcept;
    std::span<const std::uint8_t> bytes() const noexcept;
    void advance(std::uint64_t ticks) { hardware_.advance(ticks, ram_); }
    const Hardware& hardware() const noexcept { return hardware_; }
    Iop& iop() noexcept { return hardware_.iop(); }
    const Iop& iop() const noexcept { return hardware_.iop(); }
    Sio2& sio2() noexcept { return hardware_.sio2(); }
    Cdvd& cdvd() noexcept { return hardware_.cdvd(); }
    MemoryState state() const;
    void restore(const MemoryState& state);

private:
    struct Region { bool rom; std::size_t offset; };
    Region resolve(std::uint32_t address, unsigned width, Access access) const;
    Region resolve_range(std::uint32_t address, unsigned count, Access access) const;
    std::vector<std::uint8_t> ram_;
    std::vector<std::uint8_t> boot_rom_;
    Hardware hardware_;
};

} // namespace critterlink
