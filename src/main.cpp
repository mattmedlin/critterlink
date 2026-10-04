#include "critterlink/machine.hpp"
#include "critterlink/cpu.hpp"
#include "critterlink/demo.hpp"
#include "critterlink/elf.hpp"
#include "critterlink/hardware_demo.hpp"
#include "critterlink/interrupt_demo.hpp"
#include "critterlink/vector_demo.hpp"
#include "critterlink/iop_demo.hpp"
#include "critterlink/spu_demo.hpp"
#include "critterlink/io_demo.hpp"
#include "critterlink/integrated_demo.hpp"

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
        std::cout << (entry.delay_slot ? " delay-slot" : "");
        if (entry.exception) { std::cout << " exception=" << std::dec << *entry.exception << " dispatched\n"; }
        else { std::cout << (entry.stalled ? " stalled\n" : entry.retired ? " retired\n" : " stopped\n"); }
    }
    std::cout << std::dec;
}

int run_image(int argc, char** argv, bool bios) {
    const auto label = bios ? "Boot ROM" : "ELF";
    const auto usage = bios ? "Usage: critterlink --bios FILE [--steps N] [--trace] [--inspect ADDRESS]...\n" :
                              "Usage: critterlink --elf FILE [--steps N] [--trace] [--inspect ADDRESS]...\n";
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
        if (!stream) { throw std::runtime_error(std::string("cannot open ") + label + " file"); }
        const auto length = stream.tellg();
        const auto limit = bios ? critterlink::Memory::boot_rom_max_size : critterlink::maximum_elf_size;
        if (length < 0 || length > static_cast<std::streamoff>(limit)) {
            throw std::runtime_error(std::string(label) + " file exceeds its size limit or its size cannot be read");
        }
        std::vector<std::uint8_t> file(static_cast<std::size_t>(length));
        stream.seekg(0);
        if (!file.empty() && !stream.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size()))) {
            throw std::runtime_error(std::string("cannot read complete ") + label + " file");
        }
        critterlink::System system;
        auto& memory = system.memory();
        auto& cpu = system.cpu();
        if (bios) {
            memory.load_boot_rom(file);
            cpu.reset_boot_vector();
            std::cout << "Boot ROM loaded: entry=0xbfc00000 file-bytes=" << file.size() << '\n';
        } else {
            const auto image = critterlink::load_elf(file, memory, cpu);
            std::cout << "ELF loaded: entry=0x" << std::hex << std::setfill('0') << std::setw(8) << image.entry
                      << std::dec << " segments=" << image.segments << " file-bytes=" << image.file_bytes
                      << " memory-bytes=" << image.memory_bytes << '\n';
        }
        for (const auto address : inspect) { memory.read(address, 4); }
        std::cout << std::setfill('0');
        std::vector<critterlink::InstructionTrace> trace;
        const auto result = system.run(steps, tracing ? &trace : nullptr);
        print_trace(trace);
        std::cout << label << " run: retired=" << result.retired << " pc=0x" << std::hex << std::setw(8) << cpu.state().pc
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
        if (memory.hardware().stop()) {
            std::cerr << *memory.hardware().stop() << '\n';
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << label << " run failed: " << error.what() << '\n';
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
                                              "       critterlink --hardware-demo\n"
                                              "       critterlink --interrupt-demo\n"
                                              "       critterlink --vector-demo\n"
                                              "       critterlink --iop-demo\n"
                                              "       critterlink --spu-demo\n"
                                              "       critterlink --io-demo\n"
                                              "       critterlink --integrated-demo\n"
                                              "       critterlink --elf FILE [--steps N] [--trace] [--inspect ADDRESS]...\n"
                                              "       critterlink --bios FILE [--steps N] [--trace] [--inspect ADDRESS]...\n"
                                              "       critterlink --help\n";
    critterlink::Tick ticks = 0;
    if (argc == 2 && std::string_view(argv[1]) == "--integrated-demo") {
        try {
            const auto result = critterlink::run_integrated_demo();
            std::cout << "Integrated diagnostic: ticks=" << result.ticks
                      << " sprite-pixels=" << result.sprite_pixels << " samples=" << result.samples
                      << " pcm-signature=" << result.pcm_signature
                      << " concurrent=" << result.concurrent_checkpoint << " input=" << result.input
                      << " replay=" << (result.replay_identical ? "identical" : "MISMATCH") << '\n';
            return result.concurrent_checkpoint && result.graphics && result.audio && result.input &&
                   result.replay_identical ? 0 : 1;
        } catch (const std::exception& error) {
            std::cerr << "Integrated diagnostic failed: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string_view(argv[1]) == "--io-demo") {
        try {
            const auto result = critterlink::run_io_demo();
            std::cout << "I/O diagnostic: ticks=" << result.ticks << " digital=" << result.digital
                      << " analog=" << result.analog << " card=" << result.card << " disc=" << result.disc
                      << " replay=" << (result.replay_identical ? "identical" : "MISMATCH") << '\n';
            return result.digital && result.analog && result.card && result.disc && result.replay_identical ? 0 : 1;
        } catch (const std::exception& error) {
            std::cerr << "I/O diagnostic failed: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string_view(argv[1]) == "--spu-demo") {
        try {
            const auto result = critterlink::run_spu_demo();
            std::cout << "SPU2 diagnostic: ticks=" << result.ticks << " samples=" << result.samples
                      << " signature=" << result.signature << " audible=" << result.audible
                      << " released=" << result.released
                      << " replay=" << (result.replay_identical ? "identical" : "MISMATCH") << '\n';
            std::cout << "Core 0 diagnostic PCM; interpolation and physical clock ratios remain unsupported.\n";
            return result.audible && result.released && result.replay_identical ? 0 : 1;
        } catch (const std::exception& error) {
            std::cerr << "SPU2 diagnostic failed: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string_view(argv[1]) == "--iop-demo") {
        try {
            const auto result = critterlink::run_iop_demo();
            std::cout << "IOP/SIF diagnostic: ticks=" << result.ticks << " response=" << result.response
                      << " exchange-completed=" << result.exchange_completed
                      << " replay=" << (result.replay_identical ? "identical" : "MISMATCH") << '\n';
            return result.response == 40 && result.exchange_completed && result.replay_identical ? 0 : 1;
        } catch (const std::exception& error) {
            std::cerr << "IOP/SIF diagnostic failed: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string_view(argv[1]) == "--vector-demo") {
        try {
            const auto result = critterlink::run_vector_demo();
            std::cout << "Vector diagnostic: ticks=" << result.ticks << " output=";
            for (std::size_t n = 0; n < result.output.size(); ++n) {
                if (n != 0) { std::cout << ','; }
                std::cout << result.output[n];
            }
            std::cout << " dma-completed=" << result.dma_completed
                      << " replay=" << (result.replay_identical ? "identical" : "MISMATCH") << '\n';
            return result.output == std::array<std::uint32_t, 4>{7, 9, 11, 13} &&
                   result.dma_completed && result.replay_identical ? 0 : 1;
        } catch (const std::exception& error) {
            std::cerr << "Vector diagnostic failed: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string_view(argv[1]) == "--interrupt-demo") {
        try {
            const auto result = critterlink::run_interrupt_demo();
            std::cout << "Interrupt diagnostic: ticks=" << result.ticks
                      << " timer-services=" << result.timer_services << " dma-services=" << result.dma_services
                      << " sprite-pixels=" << result.colored_pixels
                      << " acknowledged=" << result.interrupts_acknowledged << " returned=" << result.returned
                      << " replay=" << (result.replay_identical ? "identical" : "MISMATCH") << '\n';
            return result.ticks == 128 && result.timer_services == 1 && result.dma_services == 1 &&
                   result.colored_pixels == 12 && result.interrupts_acknowledged && result.returned &&
                   result.replay_identical ? 0 : 1;
        } catch (const std::exception& error) {
            std::cerr << "Interrupt diagnostic failed: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string_view(argv[1]) == "--hardware-demo") {
        try {
            const auto result = critterlink::run_hardware_demo();
            std::cout << "Hardware diagnostic: ticks=" << result.ticks << " sprite-pixels=" << result.colored_pixels
                      << " int0=" << result.timer_interrupt << " int1=" << result.dma_interrupt
                      << " replay=" << (result.replay_identical ? "identical" : "MISMATCH") << '\n';
            std::cout << "Digital pad reply:";
            for (const auto byte : result.pad_reply) {
                std::cout << ' ' << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(byte);
            }
            std::cout << std::dec << "\nFilter-zero PCM:";
            for (unsigned n = 0; n < 4; ++n) { std::cout << ' ' << result.audio.samples[n]; }
            std::cout << "\nDiagnostic primitives only; full IOP/SPU2 remain unsupported. See --interrupt-demo for guest handlers.\n";
            return result.replay_identical ? 0 : 1;
        } catch (const std::exception& error) {
            std::cerr << "Hardware diagnostic failed: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--bios") {
        return run_image(argc, argv, true);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--elf") {
        return run_image(argc, argv, false);
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
