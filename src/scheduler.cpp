#include "critterlink/scheduler.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace critterlink {
namespace {
bool precedes(const ScheduledEvent& a, const ScheduledEvent& b) {
    return a.tick < b.tick || (a.tick == b.tick && a.sequence < b.sequence);
}
bool known(EventType type) {
    return type == EventType::timer || type == EventType::dma || type == EventType::input;
}
} // namespace

const SchedulerState& Scheduler::state() const noexcept { return state_; }
void Scheduler::reset() noexcept { state_ = {}; }

std::uint64_t Scheduler::schedule(std::uint64_t tick, EventType type, std::uint64_t payload) {
    if (tick < state_.now || !known(type)) {
        throw std::invalid_argument("invalid scheduler event time or type");
    }
    if (state_.next_sequence == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("scheduler sequence exhausted");
    }
    const ScheduledEvent event{tick, state_.next_sequence, type, payload};
    const auto position = std::lower_bound(state_.events.begin(), state_.events.end(), event, precedes);
    state_.events.insert(position, event);
    ++state_.next_sequence;
    return event.sequence;
}

std::optional<ScheduledEvent> Scheduler::pop_next_until(std::uint64_t target) {
    if (target < state_.now) {
        throw std::invalid_argument("scheduler cannot travel backward");
    }
    if (state_.events.empty() || state_.events.front().tick > target) {
        state_.now = target;
        return std::nullopt;
    }
    const auto event = state_.events.front();
    state_.events.erase(state_.events.begin());
    state_.now = event.tick;
    return event;
}

void Scheduler::restore(const SchedulerState& state) {
    std::unordered_set<std::uint64_t> sequences;
    if (!std::is_sorted(state.events.begin(), state.events.end(), precedes)) {
        throw std::invalid_argument("scheduler snapshot events are unordered");
    }
    for (const auto& event : state.events) {
        if (event.tick < state.now || event.sequence >= state.next_sequence ||
            !known(event.type) || !sequences.insert(event.sequence).second) {
            throw std::invalid_argument("scheduler snapshot contains invalid events");
        }
    }
    auto replacement = state;
    state_ = std::move(replacement);
}
} // namespace critterlink
