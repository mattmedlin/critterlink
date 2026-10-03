#include "critterlink/cpu.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
constexpr std::uint32_t reg(unsigned condition, unsigned code = 0) {
    return (1U << 21) | (2U << 16) | (code << 6) | (48U + condition);
}
constexpr std::uint32_t imm(unsigned condition, unsigned value) {
    return (1U << 26) | (1U << 21) | ((8U + condition) << 16) | value;
}
}
int main() {
    using namespace critterlink;
    try {
        Memory memory; Cpu cpu;
        const auto setup = [&](std::uint32_t opcode, std::uint64_t a, std::uint64_t b) {
            memory.write(0, 4, opcode);
            CpuState state;
            state.gpr[1] = {a, 0x1234}; state.gpr[2] = {b, 0x9876};
            state.gpr[3] = {44, 55}; state.hi = {66, 77}; state.lo = {88, 99};
            cpu.restore(state);
        };
        // Expected outcomes are literal signed/unsigned boundary comparisons.
        struct Example { std::uint64_t a, b; std::array<bool, 6> taken; };
        constexpr std::array<unsigned, 6> conditions{0, 1, 2, 3, 4, 6};
        const std::array<Example, 5> examples{{
            {0, 0, {true,true,false,false,true,false}},
            {1, 2, {false,false,true,true,false,true}},
            {0xffffffffffffffffULL, 0, {false,true,true,false,false,true}},
            {0x8000000000000000ULL, 0x7fffffffffffffffULL, {false,true,true,false,false,true}},
            {0x100000000ULL, 1, {true,true,false,false,false,true}}
        }};
        for (const auto& example : examples) {
            for (unsigned n = 0; n < conditions.size(); ++n) {
                for (unsigned code : {0U, 1U, 1023U}) {
                    setup(reg(conditions[n], code), example.a, example.b);
                    const auto before = cpu.state();
                    const auto trace = cpu.step(memory);
                    check(!trace.stop && trace.retired == !example.taken[n], "register trap retirement");
                    check(trace.exception == (example.taken[n] ? std::optional<std::uint32_t>{13} : std::nullopt), "register predicate");
                    check(cpu.state().gpr == before.gpr && cpu.state().hi == before.hi && cpu.state().lo == before.lo, "trap changed operands");
                    check(cpu.state().pc == (example.taken[n] ? 0x80000180U : 4U), "register trap vector");
                }
            }
        }
        const std::array<Example, 4> immediate_examples{{
            {0, 0, {true,true,false,false,true,false}},
            {0, 0xffff, {true,false,false,true,false,true}},
            {0xffffffffffffffffULL, 0xffff, {true,true,false,false,true,false}},
            {0x8000, 0x8000, {true,false,false,true,false,true}}
        }};
        for (const auto& example : immediate_examples) {
            for (unsigned n = 0; n < conditions.size(); ++n) {
                setup(imm(conditions[n], static_cast<unsigned>(example.b)), example.a, 123);
                const auto trace = cpu.step(memory);
                check(!trace.stop && trace.retired == !example.taken[n] &&
                    trace.exception.has_value() == example.taken[n], "immediate sign extension/predicate");
            }
        }
        for (auto opcode : {reg(5), reg(7), imm(5,0), imm(7,0)}) {
            setup(opcode,0,0);
            check(cpu.step(memory).stop.has_value(), "reserved trap selector accepted");
        }
        for (auto opcode : {reg(4,1023), imm(4,7)}) {
            setup(0x10000003,7,7); memory.write(4,4,opcode);
            check(cpu.step(memory).retired, "branch before trap");
            check(cpu.step(memory).exception == 13U && cpu.state().cop0.epc == 0 &&
                (cpu.state().cop0.cause & 0x80000000U) && !cpu.state().delay_slot, "delay trap context");
        }
        setup(0x10000003,7,7); memory.write(4,4,imm(6,7));
        check(cpu.step(memory).retired && cpu.step(memory).retired && cpu.state().pc == 16, "false delay trap lost branch");
        setup(0x50220003,1,2); memory.write(4,4,reg(6));
        check(cpu.step(memory).retired && cpu.state().pc == 8, "likely branch did not annul trap");
        setup(reg(4),3,3);
        auto state = cpu.state(); state.cop0.status = 0x400000; cpu.restore(state);
        check(cpu.step(memory).exception == 13U && cpu.state().pc == 0xbfc00380U, "BEV trap vector");
        setup(reg(4),3,3); state = cpu.state(); state.cop0.status = 2;
        state.cop0.epc = 0x1234; state.cop0.cause = 0x80000024; cpu.restore(state);
        check(cpu.step(memory).exception == 13U && cpu.state().cop0.epc == 0x1234 &&
            (cpu.state().cop0.cause & 0x8000007cU) == 0x80000034U, "nested EXL trap context");
        // Guest handler advances EPC and returns; replay starts inside the handler.
        setup(reg(4),3,3);
        memory.write(0x180,4,0x40017000); // MFC0 r1,EPC
        memory.write(0x184,4,0x24210004); // ADDIU r1,r1,4
        memory.write(0x188,4,0x40817000); // MTC0 r1,EPC
        memory.write(0x18c,4,0x42000018); // ERET
        memory.write(4,4,0x2403002a);
        check(cpu.step(memory).exception == 13U && cpu.step(memory).retired, "handler entry");
        const auto checkpoint = cpu.state();
        std::vector<InstructionTrace> first, second;
        check(cpu.run(memory,4,&first) == RunResult{4,true}, "handler baseline");
        const auto expected = cpu.state(); cpu.restore(checkpoint);
        for (unsigned n=0;n<4;++n) check(cpu.run(memory,1,&second) == RunResult{1,true}, "handler replay");
        check(first == second && cpu.state() == expected && cpu.state().gpr[3].low == 42 &&
            !(cpu.state().cop0.status & 2U), "handler replay/ERET result");
        std::cout << "EE trap tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
