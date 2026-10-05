#pragma once

#include <cstdint>

namespace critterlink {

struct IopIntcState {
    std::uint32_t status{}, mask{}, levels{};
    bool enabled{};
    bool operator==(const IopIntcState&) const = default;
};

class IopIntc {
public:
    static constexpr std::uint32_t source_mask = 0x03ffffff;
    static bool address(std::uint32_t physical) noexcept;
    const IopIntcState& state() const noexcept { return state_; }
    void restore(IopIntcState state);
    // Sample source levels; only rising edges latch status, independent of masks.
    void sample(std::uint32_t levels);
    // Latch already-qualified rising events (for timer pulse/toggle requests).
    void raise_edges(std::uint32_t sources);
    bool irq() const noexcept { return state_.enabled && (state_.status & state_.mask) != 0; }
    // Word registers, plus the documented byte access to I_CTRL.
    std::uint32_t read(std::uint32_t physical, unsigned width = 4);
    void write(std::uint32_t physical, std::uint32_t value, unsigned width = 4);
private:
    IopIntcState state_;
};

} // namespace critterlink
