#include "critterlink/cpu.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr std::uint32_t r(unsigned fn, unsigned rs=1, unsigned rt=2, unsigned rd=3, unsigned sa=0) {
    return (rs<<21U)|(rt<<16U)|(rd<<11U)|(sa<<6U)|fn;
}
constexpr std::uint32_t i(unsigned op, unsigned imm, unsigned rt=3) {
    return (op<<26U)|(1U<<21U)|(rt<<16U)|imm;
}
constexpr std::uint64_t sign=0x8000000000000000ULL, max=0x7fffffffffffffffULL;
}
int main() {
    using namespace critterlink;
    try {
        Memory memory;
        Cpu cpu;
        const auto setup = [&](std::uint32_t opcode, std::uint64_t a, std::uint64_t b) {
            memory.write(0,4,opcode);
            CpuState state;
            state.gpr[1]={a,0x1111}; state.gpr[2]={b,0x2222}; state.gpr[3]={0xabcdef,0x3333};
            cpu.restore(state);
        };
        struct Case { std::uint32_t opcode; std::uint64_t a,b,result; };
        const std::array cases{
            Case{r(44),max,0,max}, Case{r(44),sign,1,sign+1},
            Case{r(45),max,1,sign}, Case{r(45),~0ULL,1,0},
            Case{r(46),sign+1,1,sign}, Case{r(46),max,max,0},
            Case{r(47),0,1,~0ULL}, Case{r(47),sign,1,max},
            Case{i(24,0xffff),0,0,~0ULL}, Case{i(24,0x8000),32768,0,0},
            Case{i(25,1),max,0,sign}, Case{i(25,0xffff),sign,0,max},
            Case{r(56,0,2,3,31),0,3,0x180000000ULL},
            Case{r(60,0,2,3,31),0,1,sign}, Case{r(60,0,2,3),0,1,0x100000000ULL},
            Case{r(58,0,2,3,31),0,sign,0x100000000ULL},
            Case{r(62,0,2,3,31),0,sign,1}, Case{r(62,0,2,3),0,sign,0x80000000ULL},
            Case{r(59,0,2,3,31),0,sign,0xffffffff00000000ULL},
            Case{r(63,0,2,3,31),0,sign,~0ULL}, Case{r(63,0,2,3),0,sign,0xffffffff80000000ULL},
            Case{r(20),63,1,sign}, Case{r(20),64,sign,sign},
            Case{r(22),127,sign,1}, Case{r(23),63,sign,~0ULL},
            Case{r(23),64,sign,sign}, Case{r(59,0),0,sign,sign},
            Case{r(10),0x123456789abcdef0ULL,0,0x123456789abcdef0ULL},
            Case{r(10),42,0x100000000ULL,0xabcdef},
            Case{r(11),42,0,0xabcdef}, Case{r(11),42,0x100000000ULL,42}
        };
        for (const auto& c: cases) {
            setup(c.opcode,c.a,c.b);
            const auto trace=cpu.step(memory);
            check(trace.retired && !trace.stop && !trace.exception,"scalar instruction did not retire");
            check(cpu.state().gpr[3]==Register128{c.result,0x3333},"scalar result/upper lane mismatch");
            check(cpu.state().gpr[0]==Register128{},"r0 changed");
        }
        for (const auto& c: std::array{
            Case{r(44),max,1,0},Case{r(44),sign,~0ULL,0},
            Case{r(46),sign,1,0},Case{r(46),max,~0ULL,0},
            Case{i(24,1),max,0,0},Case{i(24,0xffff),sign,0,0}}) {
            for (bool zero: {false,true}) {
                const auto opcode=zero ? (c.opcode & ~((c.opcode>>26U)==24 ? (31U<<16U):(31U<<11U))) : c.opcode;
                setup(opcode,c.a,c.b);
                const auto before=cpu.state().gpr;
                const auto trace=cpu.step(memory);
                check(!trace.retired && trace.exception==12U && !trace.stop,"overflow not dispatched");
                check(cpu.state().gpr==before && cpu.state().pc==0x80000180 && cpu.state().cop0.epc==0,
                    "overflow committed destination or wrong vector");
            }
        }
        for (auto opcode: {r(44,1,2,3,1),r(47,1,2,3,1),r(10,1,2,3,1),r(11,1,2,3,1),
                           r(56,1),r(63,1),r(20,1,2,3,1),r(22,1,2,3,1),r(23,1,2,3,1)}) {
            setup(opcode,1,2);
            const auto before=cpu.state().gpr;
            check(cpu.step(memory).stop.has_value() && cpu.state().gpr==before,"reserved encoding accepted");
        }
        // Overflow in a branch delay slot preserves the branch EPC and BD.
        setup(0x10000001,max,1); memory.write(4,4,r(44));
        check(cpu.step(memory).retired,"branch setup");
        check(cpu.step(memory).exception==12U && cpu.state().cop0.epc==0 &&
            (cpu.state().cop0.cause&0x80000000U),"doubleword delay-slot overflow context");
        // Aliased destination reads old operands; repeated execution preserves upper lanes.
        setup(r(45,1,2,1),5,7);
        check(cpu.step(memory).retired && cpu.state().gpr[1]==Register128{12,0x1111},"source alias arithmetic");
        setup(i(25,0xffff),0,0);
        memory.write(4,4,r(60,0,3,4)); memory.write(8,4,r(23,2,4,5));
        memory.write(12,4,r(10,5,0,6));
        const auto checkpoint=cpu.state();
        std::vector<InstructionTrace> first,second;
        check(cpu.run(memory,4,&first)==RunResult{4,true},"scalar replay baseline");
        const auto expected=cpu.state(); cpu.restore(checkpoint);
        for (unsigned n=0;n<4;++n) check(cpu.run(memory,1,&second)==RunResult{1,true},"scalar replay step");
        check(cpu.state()==expected && first==second,"scalar snapshot replay differs");
        std::cout << "EE scalar extension tests passed\n";
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
