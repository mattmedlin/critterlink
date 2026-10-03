#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace critterlink {

struct SifEeDmaState {
    std::uint32_t chcr{}, address{}, qwords{};
    bool operator==(const SifEeDmaState&) const = default;
};
struct SifIopDmaState {
    std::uint32_t address{}, blocks{}, chcr{};
    std::uint8_t block_words{};
    bool operator==(const SifIopDmaState&) const = default;
};
struct SifState {
    std::uint32_t mscom{}, smcom{}, msflag{}, smflag{};
    SifEeDmaState ee_receive, ee_send;
    SifIopDmaState iop_send, iop_receive;
    std::uint32_t iop_dpcr2{}, iop_dma_enable{}, iop_dicr{}, iop_dicr2{};
    std::uint32_t ee_completions{};
    std::vector<std::array<std::uint8_t, 16>> to_ee, to_iop;
    std::optional<std::string> stop;
    bool operator==(const SifState&) const = default;
};

// Normal EE DMA / 32-word IOP block transfers; no SIF command or RPC HLE.
class Sif {
public:
    static constexpr std::size_t fifo_capacity = 8;
    static bool ee_address(std::uint32_t address) noexcept;
    static bool iop_address(std::uint32_t address) noexcept;
    std::uint32_t ee_read32(std::uint32_t address) const;
    void ee_write32(std::uint32_t address, std::uint32_t value);
    std::uint32_t iop_read32(std::uint32_t address) const;
    void iop_write32(std::uint32_t address, std::uint32_t value);
    void tick(std::span<std::uint8_t> ee_ram, std::span<std::uint8_t> iop_ram,
              bool ee_dma_enabled);
    std::uint32_t take_ee_completions() noexcept;
    bool iop_irq() const noexcept;
    const SifState& state() const noexcept { return state_; }
    void restore(const SifState& state);
private:
    SifState state_;
};

} // namespace critterlink
