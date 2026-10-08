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
    explicit MemoryStall(const char* message = "GIF FIFO is full; retry after device progress") : std::runtime_error(message) {}
};

struct MemoryState {
    std::vector<std::uint8_t> ram;
    HardwareState hardware;
    std::vector<std::uint8_t> boot_rom;
    std::vector<std::uint8_t> scratchpad = std::vector<std::uint8_t>(16384);
    bool operator==(const MemoryState&) const = default;
};

// Physical backing plus legacy diagnostic aliases. CPU owns TLB translation
// and its instruction/data caches and accelerated read buffer.
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
    // Aligned memory accesses and GIF FIFO writes; a full FIFO throws MemoryStall.
    // EE LQ/SQ mask the effective address in the CPU.
    std::array<std::uint64_t, 2> read_quadword(std::uint32_t address);
    std::array<std::uint64_t, 2> read_quadword(std::uint32_t address) const;
    void write_quadword(std::uint32_t address, const std::array<std::uint64_t, 2>& value);
    // Byte-enabled scratchpad backing. CPU alignment/translation is checked separately.
    std::uint64_t read_scratchpad(std::uint32_t offset,unsigned count) const;
    void write_scratchpad(std::uint32_t offset,unsigned count,std::uint64_t value);
    void clear() noexcept;
    std::span<const std::uint8_t> bytes() const noexcept;
    void advance(std::uint64_t ticks) { hardware_.advance(ticks, ram_, boot_rom_); }
    void set_ee_hblank(bool active) noexcept { hardware_.set_ee_hblank(active); }
    void set_ee_vblank(bool active) noexcept { hardware_.set_ee_vblank(active); }
    void set_sbus_interrupt_line(bool high) noexcept { hardware_.set_sbus_interrupt_line(high); }
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
    std::vector<std::uint8_t> scratchpad_ = std::vector<std::uint8_t>(16384);
    Hardware hardware_;
};

} // namespace critterlink
