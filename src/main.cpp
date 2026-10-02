#include "critterlink/machine.hpp"

#include <charconv>
#include <iostream>
#include <string_view>
#include <system_error>

int main(int argc, char** argv) {
    constexpr std::string_view usage = "Usage: critterlink [--ticks <unsigned integer>]\n"
                                              "       critterlink --help\n";
    critterlink::Tick ticks = 0;
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << usage << "Foundation only; PS2 programs cannot run yet.\n";
        return 0;
    }
    if (argc != 1) {
        if (argc != 3 || std::string_view(argv[1]) != "--ticks") {
            std::cerr << usage;
            return 2;
        }
        const std::string_view value(argv[2]);
        const auto result = std::from_chars(value.data(), value.data() + value.size(), ticks);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
            std::cerr << "Invalid tick count: expected an unsigned 64-bit decimal integer.\n";
            return 2;
        }
    }
    critterlink::Machine machine;
    machine.advance(ticks);
    std::cout << "Critterlink foundation: tick=" << machine.state().tick
              << " (PS2 execution not implemented)\n";
    return 0;
}
