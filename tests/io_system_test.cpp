#include "critterlink/io_demo.hpp"

#include <iostream>
#include <memory>
#include <type_traits>
#include <stdexcept>

namespace {
template<class T> auto snap(const T& object) {
    using State = std::remove_cvref_t<decltype(object.state())>;
    return std::unique_ptr<State>(new State(object.state()));
}
void check(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}
template<class F> void rejects(F function) {
    try { function(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid media snapshot accepted");
}
}

int main() {
    using namespace critterlink;
    try {
        const auto demo = run_io_demo();
        check(demo.digital && demo.analog && demo.card && demo.disc && demo.replay_identical,
              "guest controller/card/disc signatures or replay differ");
        auto system = std::make_unique<System>(io_demo_input());
        prepare_io_demo(*system);
        for (unsigned n = 0; n < 4096 && !system->memory().cdvd().state().reading; ++n) {
            check(system->run(1).budget_exhausted, "disc setup stopped");
        }
        const auto mid = snap(*system);
        check(mid->memory.hardware.cdvd.reading && mid->memory.hardware.cdvd.transferred == 16,
              "disc checkpoint is not mid DMA sector");
        std::vector<InstructionTrace> first, replay;
        check(system->run(200, &first).budget_exhausted, "disc continuation stopped");
        const auto expected = snap(*system);
        system->restore(*mid);
        check(system->run(73, &replay).budget_exhausted && system->run(127, &replay).budget_exhausted,
              "partitioned disc replay stopped");
        check(*snap(*system) == *expected && first == replay, "mid disc replay differs");
        auto bad = std::unique_ptr<SystemState>(new SystemState(*mid));
        bad->memory.hardware.cdvd.media[0] ^= 1;
        rejects([&] { system->restore(*bad); });
        check(*snap(*system) == *expected, "wrong disc restore mutated live system");
        *bad = *mid;
        bad->memory.hardware.sio2.cards[0].identity = "different-card";
        rejects([&] { system->restore(*bad); });
        check(*snap(*system) == *expected, "wrong card restore mutated live system");
        check(system->run(0) == RunResult{0, true} && *snap(*system) == *expected,
              "zero budget advanced I/O");
        auto card_system = std::make_unique<System>(io_demo_input());
        prepare_io_demo(*card_system);
        for (unsigned n = 0; n < 4096; ++n) {
            check(card_system->run(1).budget_exhausted, "card setup stopped");
            const auto& sio = card_system->memory().sio2().state();
            if (sio.active && sio.input.size() > 2 && sio.input[0] == 0x81 &&
                sio.input[1] == 0x42 && sio.byte_cursor == 3) { break; }
        }
        const auto card_mid = snap(*card_system);
        check(card_mid->memory.hardware.sio2.active && card_mid->memory.hardware.sio2.input[1] == 0x42,
              "card checkpoint is not mid write");
        check(card_system->run(100).budget_exhausted, "card continuation stopped");
        const auto card_expected = snap(*card_system);
        card_system->restore(*card_mid);
        check(card_system->run(37).budget_exhausted && card_system->run(63).budget_exhausted &&
              *snap(*card_system) == *card_expected, "mid card write restoration differs");
        std::cout << "I/O system tests passed ticks=" << demo.ticks << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
