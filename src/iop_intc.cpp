#include "critterlink/iop_intc.hpp"

#include <stdexcept>
#include <utility>

namespace critterlink {
namespace {
constexpr std::uint32_t stat = 0x1f801070, mask = 0x1f801074, control = 0x1f801078;
void validate_access(std::uint32_t address, unsigned width) {
    if (!((width == 4 && (address == stat || address == mask || address == control)) ||
          (width == 1 && address == control))) {
        throw std::invalid_argument("unsupported IOP INTC access width or address");
    }
}
}

bool IopIntc::address(std::uint32_t physical) noexcept {
    return physical >= stat && physical < control + 4;
}
void IopIntc::restore(IopIntcState state) {
    if (((state.status | state.mask | state.levels) & ~source_mask) != 0) {
        throw std::invalid_argument("invalid IOP INTC snapshot");
    }
    state_ = state;
}
void IopIntc::sample(std::uint32_t levels) {
    if ((levels & ~source_mask) != 0) {
        throw std::invalid_argument("invalid IOP interrupt source");
    }
    state_.status |= levels & ~state_.levels;
    state_.levels = levels;
}
void IopIntc::raise_edges(std::uint32_t sources) {
    if ((sources & ~source_mask) != 0) {
        throw std::invalid_argument("invalid IOP interrupt event source");
    }
    state_.status |= sources;
}
std::uint32_t IopIntc::read(std::uint32_t physical, unsigned width) {
    validate_access(physical, width);
    if (physical == stat) { return state_.status; }
    if (physical == mask) { return state_.mask; }
    return std::exchange(state_.enabled, false) ? 1U : 0U;
}
void IopIntc::write(std::uint32_t physical, std::uint32_t value, unsigned width) {
    validate_access(physical, width);
    if (physical == stat) { state_.status &= value; }
    else if (physical == mask) { state_.mask = value & source_mask; }
    else { state_.enabled = (value & 1U) != 0; }
}

} // namespace critterlink
