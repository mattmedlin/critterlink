#include "critterlink/machine.hpp"
#include "critterlink/cpu.hpp"
#include "critterlink/demo.hpp"
#include "critterlink/elf.hpp"

#include <charconv>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string_view>
#include <system_error>

namespace {
bool parse_count(std::string_view value, std::uint64_t& count) {
    const auto result = std::from_chars(value.data(), value.data() + value.size(), count);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

void print_trace(const std::vector<critterlink::InstructionTrace>& trace) {
    for (const auto& entry : trace) {
        std::cout << "pc=0x" << std::hex << std::setfill('0') << std::setw(8) << entry.pc;
        if (entry.instruction) {
            std::cout << " opcode=0x" << std::setw(8) << *entry.instruction;
        }
        std::cout << (entry.delay_slot ? " delay-slot" : "") << (entry.retired ? " retired\n" : " stopped\n");
    }
    std::cout << std::dec;
}

int run_elf(int argc, char** argv) {
    constexpr auto usage = "Usage: critterlink --elf FILE [--steps N] [--trace] [--inspect ADDRESS]...\n";
    if (argc < 3) {
        std::cerr << usage;
        return 2;
    }
    std::uint64_t steps = 1000;
    bool tracing = false;
    bool has_steps = false;
    std::vector<std::uint32_t> inspect;
    for (int n = 3; n < argc; ++n) {
        const std::string_view argument(argv[n]);
        if (argument == "--trace" && !tracing) {
            tracing = true;
        } else if (argument == "--steps" && !has_steps && n + 1 < argc) {
            has_steps = true;
            if (!parse_count(argv[++n], steps) || steps > 100000) {
                std::cerr << "Invalid step budget: expected 0 through 100000.\n";
                return 2;
            }
        } else if (argument == "--inspect" && n + 1 < argc && inspect.size() < 16) {
            std::string_view value(argv[++n]);
            int base = 10;
            if (value.starts_with("0x") || value.starts_with("0X")) { value.remove_prefix(2); base = 16; }
            std::uint32_t address = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), address, base);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || address % 4 != 0) {
                std::cerr << "Invalid inspect address: expected an aligned unsigned 32-bit address.\n";
                return 2;
            }
            inspect.push_back(address);
        } else {
            std::cerr << usage;
            return 2;
        }
    }
    try {
        std::ifstream stream(argv[2], std::ios::binary | std::ios::ate);
        if (!stream) { throw std::runtime_error("cannot open ELF file"); }
        const auto length = stream.tellg();
        if (length < 0 || length > static_cast<std::streamoff>(critterlink::maximum_elf_size)) {
            throw std::runtime_error("ELF file exceeds the 64 MiB limit or its size cannot be read");
        }
        std::vector<std::uint8_t> file(static_cast<std::size_t>(length));
        stream.seekg(0);
        if (!file.empty() && !stream.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size()))) {
            throw std::runtime_error("cannot read complete ELF file");
        }
        critterlink::Memory memory;
        critterlink::Cpu cpu;
        for (const auto address : inspect) { memory.read(address, 4); }
        const auto image = critterlink::load_elf(file, memory, cpu);
        std::cout << "ELF loaded: entry=0x" << std::hex << std::setfill('0') << std::setw(8) << image.entry
                  << std::dec << " segments=" << image.segments << " file-bytes=" << image.file_bytes
                  << " memory-bytes=" << image.memory_bytes << '\n';
        std::vector<critterlink::InstructionTrace> trace;
        const auto result = cpu.run(memory, steps, tracing ? &trace : nullptr);
        print_trace(trace);
        std::cout << "ELF run: retired=" << result.retired << " pc=0x" << std::hex << std::setw(8) << cpu.state().pc
                  << (result.budget_exhausted ? " budget-exhausted\n" : " stopped\n");
        for (const auto address : inspect) {
            std::cout << "inspect[0x" << std::setw(8) << address << "]=0x"
                      << std::setw(8) << memory.read(address, 4) << '\n';
        }
        std::cout << std::dec;
        if (cpu.state().stop) {
            std::cerr << cpu.state().stop->diagnostic << '\n';
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ELF run failed: " << error.what() << '\n';
        return 1;
    }
}

int run_demo(int argc, char** argv) {
    auto steps = critterlink::demo_instruction_count;
    bool tracing = false;
    bool has_steps = false;
    for (int n = 2; n < argc; ++n) {
        const std::string_view argument(argv[n]);
        if (argument == "--trace" && !tracing) {
            tracing = true;
        } else if (argument == "--steps" && !has_steps && n + 1 < argc) {
            has_steps = true;
            if (!parse_count(argv[++n], steps) || steps > 100000) {
                std::cerr << "Invalid step budget: expected 0 through 100000.\n";
                return 2;
            }
        } else {
            std::cerr << "Usage: critterlink --demo [--steps N] [--trace]\n";
            return 2;
        }
    }
    critterlink::Memory memory;
    critterlink::load_demo(memory);
    critterlink::Cpu cpu;
    std::vector<critterlink::InstructionTrace> trace;
    const auto result = cpu.run(memory, steps, tracing ? &trace : nullptr);
    print_trace(trace);
    std::cout << std::dec << "CPU slice: retired=" << result.retired
              << " pc=" << cpu.state().pc << " r2=" << cpu.state().gpr[2].low
              << " r3=" << cpu.state().gpr[3].low << " r4=" << cpu.state().gpr[4].low
              << " r5=" << cpu.state().gpr[5].low << " ram[256]=" << memory.read(256, 4)
              << (result.budget_exhausted ? " budget-exhausted\n" : " stopped\n");
    if (cpu.state().stop) {
        std::cerr << cpu.state().stop->diagnostic << '\n';
        return 1;
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    constexpr std::string_view usage = "Usage: critterlink [--ticks <unsigned integer>]\n"
                                              "       critterlink --demo [--steps N] [--trace]\n"
                                              "       critterlink --elf FILE [--steps N] [--trace] [--inspect ADDRESS]...\n"
                                              "       critterlink --help\n";
    critterlink::Tick ticks = 0;
    if (argc >= 2 && std::string_view(argv[1]) == "--elf") {
        return run_elf(argc, argv);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--demo") {
        return run_demo(argc, argv);
    }
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << usage << "Experimental CPU slice; PS2 boot and game execution are unsupported.\n";
        return 0;
    }
    if (argc != 1) {
        if (argc != 3 || std::string_view(argv[1]) != "--ticks") {
            std::cerr << usage;
            return 2;
        }
        const std::string_view value(argv[2]);
        if (!parse_count(value, ticks)) {
            std::cerr << "Invalid tick count: expected an unsigned 64-bit decimal integer.\n";
            return 2;
        }
    }
    critterlink::Machine machine;
    machine.advance(ticks);
    std::cout << "Critterlink foundation: tick=" << machine.state().tick
              << " (logical clock only; use --demo for the CPU slice)\n";
    return 0;
}
