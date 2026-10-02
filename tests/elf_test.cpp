#include "critterlink/elf.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
using namespace critterlink;
void check(bool value, const std::string& message) {
    if (!value) { throw std::runtime_error(message); }
}
void put16(std::vector<std::uint8_t>& file, std::size_t at, std::uint32_t value) {
    file.at(at) = static_cast<std::uint8_t>(value);
    file.at(at + 1) = static_cast<std::uint8_t>(value >> 8);
}
void put32(std::vector<std::uint8_t>& file, std::size_t at, std::uint32_t value) {
    put16(file, at, value);
    put16(file, at + 2, value >> 16);
}

void test_execution(const std::vector<std::uint8_t>& file) {
    Memory memory;
    Cpu cpu(0x80000000);
    auto dirty = cpu.state();
    dirty.gpr[1] = {9, 9};
    dirty.gpr[29].low = 123;
    dirty.delay_slot = true;
    dirty.cop0.epc = 7;
    dirty.stop = CpuStop{StopKind::unsupported_instruction, 0, {}, "old fault"};
    cpu.restore(dirty);
    memory.write(0x80, 4, 0xfeedface);
    for (std::uint32_t at = 0x00101000; at < 0x00101028; at += 4) {
        memory.write(at, 4, 0xffffffff);
    }
    const auto info = load_elf(file, memory, cpu);
    check(info.entry == 0x00100000 && info.segments == 2 && info.file_bytes == 84 && info.memory_bytes == 116,
          "ELF image metadata");
    Cpu clean(0x00100000);
    check(cpu.state() == clean.state(), "loader did not reset initial execution state");
    check(memory.read(0x00100000, 4) == 0x3c060010 && memory.read(0x00101000, 4) == 0x12345678,
          "text or initialized data not loaded");
    for (std::uint32_t at = 0x00101004; at < 0x00101024; at += 4) {
        check(memory.read(at, 4) == 0, "BSS did not clear prior RAM contents");
    }
    check(memory.read(0x00101024, 4) == 0xffffffff && memory.read(0x80, 4) == 0xfeedface,
          "loader wrote outside segment ranges");
    std::vector<InstructionTrace> trace;
    check(cpu.run(memory, 34, &trace) == RunResult{34, true}, "fixture completion budget");
    check(cpu.state().pc == 0x00100048 && !cpu.state().delay_slot, "fixture completion PC");
    check(memory.read(0x00101010, 4) == 0x4b4e4c43, "completion marker");
    check(memory.read(0x00101014, 4) == 15 && memory.read(0x00101018, 4) == 5, "sum/count signature");
    check(memory.read(0x0010101c, 4) == 0x12345678 && memory.read(0x00101020, 4) == 1,
          "initialized data/BSS signature");
    check(cpu.state().gpr[29].low == 0, "fixture unexpectedly needs an initialized stack");
    const auto final_state = cpu.state();
    const std::vector<std::uint8_t> final_memory(memory.bytes().begin(), memory.bytes().end());
    load_elf(file, memory, cpu);
    std::vector<InstructionTrace> replay;
    for (unsigned n = 0; n < 34; ++n) { cpu.run(memory, 1, &replay); }
    check(cpu.state() == final_state && trace == replay, "ELF replay state/trace differs");
    check(std::memcmp(final_memory.data(), memory.bytes().data(), final_memory.size()) == 0,
          "ELF replay RAM differs");
    check(cpu.run(memory, 66) == RunResult{66, true} && cpu.state().pc == 0x00100048,
          "fixture park loop is not bounded");
}

void test_rejections(const std::vector<std::uint8_t>& fixture) {
    Memory memory;
    memory.write(0x00100000, 4, 0x11223344);
    memory.write(0x00101020, 4, 0x55667788);
    memory.write(0x80, 4, 0x12345678);
    Cpu cpu(0x1234);
    auto state = cpu.state();
    state.gpr[3] = {1, 2};
    state.delay_slot = true;
    state.branch_pc = 0x1230;
    state.stop = CpuStop{StopKind::exception, 0x1234, 12, "existing stop"};
    cpu.restore(state);
    const auto original_cpu = cpu.state();
    const std::vector<std::uint8_t> original_memory(memory.bytes().begin(), memory.bytes().end());
    const auto reject = [&](const std::vector<std::uint8_t>& file, const std::string& name) {
        bool caught = false;
        try { load_elf(file, memory, cpu); }
        catch (const ElfError& error) { caught = std::string(error.what()).starts_with("ELF:"); }
        check(caught, "malformed ELF accepted: " + name);
        check(cpu.state() == original_cpu, "rejected ELF mutated CPU: " + name);
        check(std::memcmp(original_memory.data(), memory.bytes().data(), original_memory.size()) == 0,
              "rejected ELF mutated RAM: " + name);
    };
    // Every proper prefix is truncated: initialized data occupies the last word.
    for (std::size_t size = 0; size < fixture.size(); ++size) {
        reject(std::vector<std::uint8_t>(fixture.begin(), fixture.begin() + static_cast<std::ptrdiff_t>(size)),
               "truncated prefix " + std::to_string(size));
    }
    const auto patch32 = [&](std::size_t at, std::uint32_t value, const char* name) {
        auto file = fixture; put32(file, at, value); reject(file, name);
    };
    const auto patch16 = [&](std::size_t at, std::uint32_t value, const char* name) {
        auto file = fixture; put16(file, at, value); reject(file, name);
    };
    struct IdentChange { std::size_t offset; std::uint8_t value; };
    const IdentChange ident_changes[]{{0, 0}, {4, 2}, {5, 2}, {6, 0}, {7, 3}, {8, 1}};
    for (const auto& [at, value] : ident_changes) {
        auto file = fixture; file.at(at) = value; reject(file, "ident");
    }
    patch16(16, 3, "ET_DYN"); patch16(18, 62, "wrong machine"); patch32(20, 2, "header version");
    patch32(36, 0x20000002, "PIC flags"); patch32(36, 0x70000000, "wrong ISA");
    patch16(40, 0, "ELF header size"); patch16(42, 0, "program header size");
    patch16(44, 0, "no program headers"); patch16(44, 129, "too many program headers");
    patch32(28, 0xfffffff0, "program table overflow"); patch32(28, 0, "program table inside header");
    patch32(24, 0x00100001, "unaligned entry"); patch32(24, 0x00101000, "non-executable entry");
    patch32(24, 0x00100050, "entry past code"); patch32(24, 0xffffffff, "entry wrap");
    patch32(52, 2, "dynamic segment"); patch32(52, 3, "interpreter segment");
    patch32(52, 0x70000000, "unsupported processor metadata");
    patch32(56, 0xfffffffc, "segment file range overflow");
    patch32(68, 81, "file size exceeds memory size"); patch32(68, 0xffffffff, "huge file size");
    patch32(72, 0xffffffff, "memory range overflow");
    patch32(60, 0x80000000, "alias destination outside profile");
    patch32(80, 3, "non-power-of-two alignment"); patch32(80, 0x1000, "alignment incongruence");
    patch32(76, 4, "entry lacks execute flag"); patch32(76, 0x80000005, "unknown permission flags");
    patch32(92, 0x00100000, "second segment overlaps first");
    patch32(100, 0xffffffff, "late segment invalid after valid first segment");
    patch32(104, 0xffffffff, "late BSS range overflow");
    patch16(48, 1, "section count without table"); patch16(50, 1, "section names without table");
    patch32(32, 0xfffffffc, "invalid section table");

    auto crossing = fixture;
    put32(crossing, 60, 0x01ffff00); put32(crossing, 72, 0x200); put32(crossing, 24, 0x01ffff00);
    reject(crossing, "segment crosses RAM end");
    auto bss_entry = fixture;
    put32(bss_entry, 72, 0x100); put32(bss_entry, 24, 0x00100050);
    reject(bss_entry, "entry in executable BSS");
    auto huge = fixture; huge.resize(maximum_elf_size + 1);
    reject(huge, "oversized image");

    // Table bounds are valid, but the file-backed section contents are not.
    auto sections = fixture;
    sections.resize(0x300);
    put32(sections, 32, 0x220); put16(sections, 46, 40); put16(sections, 48, 2);
    put32(sections, 0x24c, 1); put32(sections, 0x258, 0x2ff); put32(sections, 0x25c, 8);
    reject(sections, "truncated section data");
    put32(sections, 0x24c, 9);
    reject(sections, "relocation section");
}

void test_variants(const std::vector<std::uint8_t>& fixture) {
    Memory memory;
    Cpu cpu;
    auto file = fixture;
    put32(file, 80, 0); put32(file, 112, 1); // ELF's no-alignment cases
    put32(file, 64, 0xffffffff); // p_paddr is ignored by this virtual-address loader
    check(load_elf(file, memory, cpu).segments == 2, "legal alignment or ignored physical address");
    file = fixture;
    put32(file, 36, 0x20000000);
    check(load_elf(file, memory, cpu).entry == 0x00100000, "optional NOREORDER flag");
    file = fixture;
    put32(file, 100, 0); // pure BSS segment
    load_elf(file, memory, cpu);
    check(memory.read(0x00101000, 4) == 0, "pure BSS segment");
    file = fixture;
    file.resize(0x300);
    put32(file, 32, 0x220); put16(file, 46, 40); put16(file, 48, 2);
    put32(file, 0x24c, 8); put32(file, 0x258, 0xffffffff); put32(file, 0x25c, 0x24);
    check(load_elf(file, memory, cpu).segments == 2, "NOBITS section incorrectly treated as file data");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected fixture path");
        std::ifstream stream(argv[1], std::ios::binary);
        check(static_cast<bool>(stream), "cannot open fixture");
        const std::vector<std::uint8_t> fixture((std::istreambuf_iterator<char>(stream)), {});
        check(fixture.size() == 516, "fixture size");
        test_execution(fixture);
        test_rejections(fixture);
        test_variants(fixture);
        std::cout << "ELF loading, fixture execution, and rejection checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
