#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace critterlink {
struct CdvdState {
    std::vector<std::uint8_t> media;
    std::uint64_t media_id{};
    std::vector<std::uint8_t> parameters;
    std::uint8_t command{}, error{}, irq{}, howto{};
    std::uint32_t lsn{}, sectors{}, transferred{};
    bool reading{};
    std::uint32_t madr{}, bcr{}, chcr{}, dpcr{};
    std::optional<std::string> stop;
    bool operator==(const CdvdState&) const = default;
};
// Read-only 2048-byte CD sectors, normal DMA3, diagnostic timing.
class Cdvd {
public:
    static constexpr std::uint32_t sector_size = 2048;
    static constexpr std::uint32_t max_sectors = 8;
    static bool address(std::uint32_t address) noexcept;
    static bool byte_address(std::uint32_t address) noexcept;
    static bool word_address(std::uint32_t address) noexcept;
    void mount(std::vector<std::uint8_t> media);
    const CdvdState& state() const noexcept { return state_; }
    // Rejects any media mismatch, including restoration into an unmounted drive.
    void restore(const CdvdState& state);
    std::uint8_t read8(std::uint32_t address) const;
    void write8(std::uint32_t address, std::uint8_t value);
    std::uint32_t read32(std::uint32_t address) const;
    void write32(std::uint32_t address, std::uint32_t value);
    void tick(std::span<std::uint8_t> iop_ram);
    bool irq() const noexcept { return state_.irq != 0; }
private:
    CdvdState state_;
};
} // namespace critterlink
