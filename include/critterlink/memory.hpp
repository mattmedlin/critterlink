#pragma once

#include "critterlink/hardware.hpp"

#include <cstddef>
#include <cstdint>
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

struct MemoryState {
    std::vector<std::uint8_t> ram;
    HardwareState hardware;
    bool operator==(const MemoryState&) const = default;
};

// Bootstrap RAM plus explicitly supported EE MMIO. No TLB/cache/firmware yet.
class Memory {
public:
    static constexpr std::size_t ram_size = 32 * 1024 * 1024;
    Memory();
    std::uint64_t read(std::uint32_t address, unsigned width, Access access = Access::load) const;
    void write(std::uint32_t address, unsigned width, std::uint64_t value);
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
    std::size_t resolve(std::uint32_t address, unsigned width, Access access) const;
    std::vector<std::uint8_t> ram_;
    Hardware hardware_;
};

} // namespace critterlink
