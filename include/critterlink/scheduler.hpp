#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace critterlink {

enum class EventType : std::uint8_t { timer, dma, input };

struct ScheduledEvent {
    std::uint64_t tick{};
    std::uint64_t sequence{};
    EventType type{};
    std::uint64_t payload{};
    bool operator==(const ScheduledEvent&) const = default;
};

struct SchedulerState {
    std::uint64_t now{};
    std::uint64_t next_sequence{};
    std::vector<ScheduledEvent> events;
    bool operator==(const SchedulerState&) const = default;
};

// Logical ticks, not measured EE cycles. No host callbacks/pointers in state.
class Scheduler {
public:
    const SchedulerState& state() const noexcept;
    void reset() noexcept;
    std::uint64_t schedule(std::uint64_t tick, EventType type, std::uint64_t payload = 0);
    // Inclusive target. Return earliest event and advance to its tick; if none,
    // advance to target. Repeat until nullopt to finish a bounded advance.
    std::optional<ScheduledEvent> pop_next_until(std::uint64_t target);
    // Requires sorted events, unique issued sequences, known types, no past events.
    // Invalid input leaves the live state unchanged.
    void restore(const SchedulerState& state);

private:
    SchedulerState state_{};
};

} // namespace critterlink
