#include "critterlink/cpu.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
constexpr std::uint32_t opcode(unsigned op,unsigned fn,unsigned rs=1,unsigned rt=2,unsigned rd=3,unsigned sa=0) {
    return (op<<26U)|(rs<<21U)|(rt<<16U)|(rd<<11U)|(sa<<6U)|fn;
}
constexpr std::uint64_t neg1=0xffffffffffffffffULL, neg2=0xfffffffffffffffeULL;
}
int main() {
    using namespace critterlink;
    try {
        Memory memory; Cpu cpu;
        const auto setup=[&](std::uint32_t word,std::uint64_t a,std::uint64_t b) {
            memory.write(0,4,word);
            CpuState state; state.gpr[1]={a,0x1111};state.gpr[2]={b,0x2222};state.gpr[3]={123,0x3333};
            state.hi={0xabcdef1234567890ULL,0x0123456789abcdefULL};
            state.lo={0x123456789abcdef0ULL,0xfedcba9876543210ULL};
            cpu.restore(state);
        };
        for (bool pipe1: {false,true}) {
            const unsigned op=pipe1?28:0;
            for (unsigned fn: {16U,17U,18U,19U}) {
                const bool to=(fn&1U)!=0, low=(fn&2U)!=0;
                setup(opcode(op,fn,to?1:0,0,to?0:3),0xfedcba9876543210ULL,0);
                const auto before=cpu.state();
                const auto original=low?before.lo:before.hi;
                const auto selected=pipe1?original.high:original.low;
                check(cpu.step(memory).retired,"HI/LO transfer did not retire");
                const auto& after=cpu.state();
                auto expected=original;
                if(to) { if(pipe1) expected.high=before.gpr[1].low; else expected.low=before.gpr[1].low; }
                check((low?after.lo:after.hi)==expected,"selected HI/LO lane transfer");
                check((low?after.hi:after.lo)==(low?before.hi:before.lo),"unselected HI/LO changed");
                check(after.gpr[3]==(to?before.gpr[3]:Register128{selected,0x3333}),"transfer GPR upper lane");
            }
            struct Arithmetic { unsigned fn;std::uint64_t a,b,hi,lo; };
            for (const auto& c: std::array{
                Arithmetic{24,neg2,3,neg1,0xfffffffffffffffaULL},
                Arithmetic{24,0xffffffff80000000ULL,0xffffffff80000000ULL,0x40000000,0},
                Arithmetic{25,neg1,neg1,neg2,1},
                Arithmetic{25,neg1,1,0,neg1},
                Arithmetic{26,7,3,1,2}, Arithmetic{26,0xfffffffffffffff9ULL,3,neg1,neg2},
                Arithmetic{26,7,0xfffffffffffffffdULL,1,neg2},
                Arithmetic{26,0xfffffffffffffff9ULL,0xfffffffffffffffdULL,neg1,2},
                Arithmetic{27,neg1,1,0,neg1},Arithmetic{27,neg1,2,1,0x7fffffff},
                Arithmetic{27,1,neg1,1,0}}) {
                const bool division=c.fn>=26;
                setup(opcode(op,c.fn,1,2,division?0:3),c.a,c.b);
                const auto before=cpu.state();
                check(cpu.step(memory).retired,"multiply/divide did not retire");
                const auto& after=cpu.state();
                check((pipe1?after.hi.high:after.hi.low)==c.hi &&
                    (pipe1?after.lo.high:after.lo.low)==c.lo,"multiply/divide fixed oracle");
                check((pipe1?after.hi.low:after.hi.high)==(pipe1?before.hi.low:before.hi.high) &&
                    (pipe1?after.lo.low:after.lo.high)==(pipe1?before.lo.low:before.lo.high),"other pipeline changed");
                check(after.gpr[3]==Register128{division?123:c.lo,0x3333},"multiply rd sign extension");
            }
            for(unsigned fn: {pipe1?32U:0U,pipe1?33U:1U}) {
                setup(opcode(28,fn),neg2,3);
                auto signedness=cpu.state();
                (pipe1?signedness.hi.high:signedness.hi.low)=0;
                (pipe1?signedness.lo.high:signedness.lo.low)=0;
                cpu.restore(signedness);
                check(cpu.step(memory).retired &&
                    (pipe1?cpu.state().hi.high:cpu.state().hi.low)==((fn&1U)?2:neg1) &&
                    (pipe1?cpu.state().lo.high:cpu.state().lo.low)==0xfffffffffffffffaULL &&
                    cpu.state().gpr[3]==Register128{0xfffffffffffffffaULL,0x3333},
                    "MADD signed/unsigned product distinction");
                setup(opcode(28,fn),1,1);
                auto start=cpu.state();
                (pipe1?start.hi.high:start.hi.low)=0xfeed000000000001ULL;
                (pipe1?start.lo.high:start.lo.low)=0xabcd0000ffffffffULL;
                cpu.restore(start);
                check(cpu.step(memory).retired,"MADD carry failed");
                check((pipe1?cpu.state().hi.high:cpu.state().hi.low)==2 &&
                    (pipe1?cpu.state().lo.high:cpu.state().lo.low)==0 && cpu.state().gpr[3].low==0,
                    "MADD must concatenate low words and propagate carry");
                setup(opcode(28,fn,1,2,0),1,1);start=cpu.state();
                (pipe1?start.hi.high:start.hi.low)=neg1;(pipe1?start.lo.high:start.lo.low)=neg1;cpu.restore(start);
                check(cpu.step(memory).retired && (pipe1?cpu.state().hi.high:cpu.state().hi.low)==0 &&
                    (pipe1?cpu.state().lo.high:cpu.state().lo.low)==0 && cpu.state().gpr[0]==Register128{},
                    "MADD wraps64 and rd0 still updates accumulator");
            }
            for(auto fn: {24U,25U,26U,27U}) {
                setup(opcode(op,fn,1,2,fn>=26?0:3),0x80000000ULL,1);
                const auto before=cpu.state();
                const auto trace=cpu.step(memory);
                check(trace.stop && !trace.exception && cpu.state().hi==before.hi && cpu.state().lo==before.lo &&
                    cpu.state().gpr==before.gpr,"noncanonical operands must stop without commit");
            }
            for(auto fn: {26U,27U}) {
                setup(opcode(op,fn,1,2,0),1,0);const auto before=cpu.state();
                check(cpu.step(memory).stop && cpu.state().hi==before.hi && cpu.state().lo==before.lo,
                    "divide zero policy failed");
            }
            setup(opcode(op,26,1,2,0),0xffffffff80000000ULL,neg1);
            check(cpu.step(memory).stop.has_value(),"signed divide overflow policy failed");
            for(auto bad: {opcode(op,16,1,0),opcode(op,17,1,1,0),opcode(op,18,0,1),
                          opcode(op,19,1,0,1),opcode(op,24,1,2,3,1),opcode(op,26,1,2,3)}) {
                setup(bad,1,2);const auto before=cpu.state();
                check(cpu.step(memory).stop && cpu.state().hi==before.hi && cpu.state().lo==before.lo &&
                    cpu.state().gpr==before.gpr,"reserved HI/LO encoding changed state");
            }
        }
        // Both accumulators and a pending instruction stream survive snapshots.
        setup(opcode(0,24),neg2,3);
        memory.write(4,4,opcode(28,24)); memory.write(8,4,opcode(28,0));
        memory.write(12,4,opcode(28,32)); memory.write(16,4,opcode(28,18,0,0,4));
        check(cpu.step(memory).retired,"initial product");const auto checkpoint=cpu.state();
        std::vector<InstructionTrace> first,second;
        check(cpu.run(memory,4,&first)==RunResult{4,true},"HI/LO replay baseline");const auto expected=cpu.state();
        cpu.restore(checkpoint);
        for(unsigned n=0;n<4;++n)check(cpu.run(memory,1,&second)==RunResult{1,true},"HI/LO replay step");
        check(cpu.state()==expected && first==second,"HI/LO replay differs");
        std::cout<<"EE HI/LO extension tests passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
