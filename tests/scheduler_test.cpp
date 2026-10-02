#include "critterlink/scheduler.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace critterlink;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Exception, class Function> void throws(Function function) {
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error("expected exception");
}
std::vector<ScheduledEvent> drain(Scheduler& scheduler, std::uint64_t target) {
    std::vector<ScheduledEvent> result;
    while (const auto event = scheduler.pop_next_until(target)) result.push_back(*event);
    return result;
}
void ordering_and_replay() {
    Scheduler scheduler;
    scheduler.schedule(7, EventType::dma, 42);
    scheduler.schedule(0, EventType::input, 9);
    scheduler.schedule(7, EventType::timer, 3);
    scheduler.schedule(8, EventType::input, 4);
    const auto first = scheduler.pop_next_until(0);
    check(first && first->payload == 9 && scheduler.state().now == 0, "tick-zero event");
    const auto snapshot = scheduler.state();
    const auto expected = drain(scheduler, 7);
    check(expected.size() == 2 && expected[0].payload == 42 && expected[1].payload == 3,
          "same-tick issue ordering or inclusive boundary");
    check(scheduler.state().events.size() == 1, "future event consumed early");
    scheduler.restore(snapshot);
    check(drain(scheduler, 7) == expected, "snapshot replay differs");
    scheduler.schedule(7, EventType::input, 55);
    check(scheduler.pop_next_until(7)->payload == 55, "same-time scheduling");

    Scheduler whole;
    whole.restore(snapshot);
    Scheduler partitioned;
    partitioned.restore(snapshot);
    const auto all = drain(whole, 10);
    std::vector<ScheduledEvent> parts;
    for (std::uint64_t tick = 0; tick <= 10; ++tick) {
        auto batch = drain(partitioned, tick);
        parts.insert(parts.end(), batch.begin(), batch.end());
    }
    check(all == parts && whole.state() == partitioned.state(), "partition-dependent scheduler");
    scheduler.reset();
    check(scheduler.state() == SchedulerState{}, "reset failed");
}
void malformed_and_limits() {
    Scheduler scheduler;
    scheduler.schedule(10, EventType::timer);
    drain(scheduler, 5);
    const auto good = scheduler.state();
    throws<std::invalid_argument>([&] { scheduler.schedule(4, EventType::dma); });
    throws<std::invalid_argument>([&] { scheduler.schedule(5, static_cast<EventType>(255)); });
    throws<std::invalid_argument>([&] { scheduler.pop_next_until(4); });
    check(scheduler.state() == good, "invalid operation mutated state");
    auto reject = [&](SchedulerState bad) {
        throws<std::invalid_argument>([&] { scheduler.restore(bad); });
        check(scheduler.state() == good, "invalid snapshot mutated state");
    };
    auto bad = good;
    bad.events[0].tick = 4;
    reject(bad);
    bad = good;
    bad.events[0].sequence = bad.next_sequence;
    reject(bad);
    bad = good;
    bad.events.push_back(bad.events[0]);
    reject(bad);
    bad = good;
    bad.events[0].type = static_cast<EventType>(255);
    reject(bad);
    bad = good;
    bad.next_sequence = 2;
    bad.events.push_back({9, 1, EventType::dma, 0});
    reject(bad);
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    scheduler.restore({maximum, maximum - 1, {}});
    check(scheduler.schedule(maximum, EventType::dma) == maximum - 1, "last sequence");
    const auto exhausted = scheduler.state();
    throws<std::overflow_error>([&] { scheduler.schedule(maximum, EventType::input); });
    check(scheduler.state() == exhausted, "sequence overflow mutated state");
    check(drain(scheduler, maximum).size() == 1, "maximum-time event lost");
}
} // namespace

int main() {
    try {
        ordering_and_replay();
        malformed_and_limits();
        std::cout << "All scheduler checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
