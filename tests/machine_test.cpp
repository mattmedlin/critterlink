#include "critterlink/machine.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Function>
void expect_throw(Function function) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("expected exception was not raised");
}

void test_playback() {
    using namespace critterlink;
    const ControllerState pressed{1, {0, 255, 128, 128}};
    const ControllerState released{};
    const std::vector<InputEvent> events{
        {0, 0, pressed}, {3, 1, pressed}, {3, 0, released}, {5, 1, released}};
    Machine machine(events);
    check(machine.state() == MachineState{}, "initial state");
    machine.advance(0);
    check(machine.state() == MachineState{}, "zero advance consumed input");
    machine.advance(1);
    check(machine.state().controllers[0] == pressed, "tick-zero input");
    machine.advance(2);
    check(machine.state().controllers[1] == released, "target event applied early");
    machine.advance(1);
    check(machine.state().controllers[0] == released, "port zero release");
    check(machine.state().controllers[1] == pressed, "port one press");
    machine.advance(2);
    const auto final_state = machine.state();
    check(final_state.tick == 6 && final_state.controllers[1] == released, "final state");
    machine.reset();
    check(machine.state() == MachineState{}, "reset state");
    machine.advance(6);
    check(machine.state() == final_state, "reset did not rewind playback");

    Machine one_step(events);
    Machine partitioned(events);
    for (Tick tick = 0; tick < 20; ++tick) {
        one_step.advance(1);
    }
    partitioned.advance(2);
    partitioned.advance(0);
    partitioned.advance(7);
    partitioned.advance(11);
    check(one_step.state() == partitioned.state(), "playback depends on host chunk size");

    Machine tied({{0, 0, pressed}, {0, 0, released}});
    tied.advance(1);
    check(tied.state().controllers[0] == released, "equal-tick ordering");

    auto source = events;
    Machine owned(source);
    source[0].state = released;
    owned.advance(1);
    check(owned.state().controllers[0] == pressed, "playback ownership");
}

void test_invalid_input_and_overflow() {
    using namespace critterlink;
    expect_throw<std::invalid_argument>([] { Machine machine({{0, 2, {}}}); });
    expect_throw<std::invalid_argument>([] { Machine machine({{3, 0, {}}, {2, 0, {}}}); });

    const auto maximum = std::numeric_limits<Tick>::max();
    const ControllerState pressed{1, {128, 128, 128, 128}};
    Machine machine({{maximum - 1, 0, pressed}, {maximum, 0, {}}});
    machine.advance(maximum - 1);
    const auto before = machine.state();
    expect_throw<std::overflow_error>([&] { machine.advance(2); });
    check(machine.state() == before, "overflow mutated state");
    machine.advance(1);
    check(machine.state().controllers[0] == pressed, "overflow consumed future event");
    machine.advance(0);
    check(machine.state().tick == maximum, "maximum tick");
    check(machine.state().controllers[0] == pressed, "maximum boundary consumed input");
    expect_throw<std::overflow_error>([&] { machine.advance(1); });
}
} // namespace

int main() {
    try {
        test_playback();
        test_invalid_input_and_overflow();
        std::cout << "All core checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
