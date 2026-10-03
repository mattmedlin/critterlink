#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace critterlink {

class IopBus {
public:
    virtual ~IopBus() = default;
    virtual std::uint8_t read8(std::uint32_t physical);
    virtual void write8(std::uint32_t physical, std::uint8_t value);
    virtual std::uint16_t read16(std::uint32_t physical);
    virtual void write16(std::uint32_t physical, std::uint16_t value);
    virtual std::uint32_t read32(std::uint32_t physical) = 0;
    virtual void write32(std::uint32_t physical, std::uint32_t value) = 0;
};

struct IopLoad {
    unsigned reg{};
    std::uint32_t value{};
    bool operator==(const IopLoad&) const = default;
};

struct IopState {
    std::array<std::uint32_t, 32> gpr{};
    std::uint32_t pc{};
    std::uint32_t next_pc{4};
    bool delay_slot{};
    std::optional<IopLoad> pending_load;
    bool enabled{};
    std::optional<std::string> stop;
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(2U * 1024U * 1024U);
    bool operator==(const IopState&) const = default;
};

// Original diagnostic MIPS-I subset, not a BIOS-bootable IOP.
class Iop {
public:
    static constexpr std::uint32_t ram_size = 2U * 1024U * 1024U;
    const IopState& state() const noexcept;
    std::span<std::uint8_t> mutable_ram() noexcept;
    void restore(IopState state);
    // Resets CPU execution state, preserving RAM.
    void start(std::uint32_t entry = 0);
    std::uint8_t read8(std::uint32_t address) const;
    void write8(std::uint32_t address, std::uint8_t value);
    std::uint16_t read16(std::uint32_t address) const;
    void write16(std::uint32_t address, std::uint16_t value);
    std::uint32_t read32(std::uint32_t address) const;
    void write32(std::uint32_t address, std::uint32_t value);
    // Returns true only if an instruction retired; faults latch an explicit stop.
    bool step(IopBus* bus = nullptr);
private:
    IopState state_{};
};

} // namespace critterlink
