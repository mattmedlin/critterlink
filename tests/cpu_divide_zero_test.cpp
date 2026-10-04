#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace critterlink;
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
constexpr std::uint32_t op(unsigned major,unsigned fn,unsigned sub=0,unsigned rs=1,unsigned rt=2,unsigned rd=0) {
    return (major<<26U)|(rs<<21U)|(rt<<16U)|(rd<<11U)|(sub<<6U)|fn;
}
struct Form { unsigned major,fn,sub; bool packed,pipeline1,unsigned_divide; };
constexpr std::array forms{
    Form{0,26,0,false,false,false},Form{0,27,0,false,false,true},
    Form{28,26,0,false,true,false},Form{28,27,0,false,true,true},
    Form{28,9,13,true,false,false},Form{28,41,13,true,false,true}};
CpuState initial() {
    CpuState s;s.hi={0x123456789abcdef0ULL,0xfedcba9876543210ULL};s.lo={77,88};s.sa=24;
    s.gpr[1]={7,9};s.gpr[2]={3,3};s.gpr[3]={42,43};return s;
}
void zero_divisors() {
    Memory memory;Cpu cpu;
    struct Vector { std::uint64_t dividend,signed_quotient; };
    // Hardware-reference inputs include zero, positive, negative and minimum word.
    constexpr std::array vectors{Vector{0,~0ULL},Vector{1,~0ULL},Vector{53,~0ULL},
        Vector{0x7fffffff,~0ULL},Vector{~0ULL,1},Vector{0xffffffffffffffcbULL,1},Vector{0xffffffff80000000ULL,1}};
    for(const auto f:forms) for(const auto v:vectors) for(unsigned mask=1;mask<(f.packed?4U:2U);++mask) {
        auto before=initial();auto expected=before;
        if(f.packed) {
            before.gpr[1]={mask&1?v.dividend:7,mask&2?v.dividend:7};before.gpr[2]={mask&1?0U:3U,mask&2?0U:3U};
            expected=before;const auto q=f.unsigned_divide?~0ULL:v.signed_quotient;
            expected.lo={mask&1?q:2,mask&2?q:2};expected.hi={mask&1?v.dividend:1,mask&2?v.dividend:1};
        } else {
            before.gpr[1]={v.dividend,0xabcdef0123456789ULL};before.gpr[2]={0,~0ULL};expected=before;
            (f.pipeline1?expected.lo.high:expected.lo.low)=f.unsigned_divide?~0ULL:v.signed_quotient;
            (f.pipeline1?expected.hi.high:expected.hi.low)=v.dividend;
        }
        const auto instruction=op(f.major,f.fn,f.sub);
        memory.write(0,4,instruction);cpu.restore(before);expected.pc=4;expected.next_pc=8;
        const auto trace=cpu.step(memory);
        check(trace.retired && !trace.stop && !trace.exception && cpu.state()==expected,"zero divide result or preserved state mismatch");
        for(auto branch:{0x10000003U,0x14000003U,0x54000003U}) {
            memory.write(0,4,branch);memory.write(4,4,instruction);cpu.restore(before);check(cpu.step(memory).retired,"zero divide branch setup");
            if(branch==0x54000003U) check(cpu.state().pc==8 && cpu.state().hi==before.hi && cpu.state().lo==before.lo,"annulled zero divide mutated state");
            else {
                const auto saved=cpu.state();const auto first=cpu.step(memory);const auto after=cpu.state();cpu.restore(saved);
                check(cpu.step(memory)==first && cpu.state()==after && first.retired && first.delay_slot &&
                    after.lo==expected.lo && after.hi==expected.hi && after.pc==(branch==0x10000003U?16U:8U),"zero divide delay replay");
            }
        }
    }
    for(const auto f:forms) {
        memory.write(0,4,op(f.major,f.fn,f.sub,0,0));auto before=initial();cpu.restore(before);
        auto expected=before;expected.pc=4;expected.next_pc=8;
        if(f.packed) {expected.hi={};expected.lo={~0ULL,~0ULL};}
        else {(f.pipeline1?expected.hi.high:expected.hi.low)=0;(f.pipeline1?expected.lo.high:expected.lo.low)=~0ULL;}
        check(cpu.step(memory).retired && cpu.state()==expected,"r0 divide sources");
    }
}
void rejection_atomicity() {
    Memory memory;Cpu cpu;
    for(const auto f:forms) for(unsigned source:{1U,2U}) {
        auto before=initial();before.gpr[2]={0,0};
        (f.packed?before.gpr[source].high:before.gpr[source].low)=0x00000000ffffffffULL;
        memory.write(0,4,op(f.major,f.fn,f.sub));cpu.restore(before);const auto trace=cpu.step(memory);before.stop=cpu.state().stop;
        check(trace.stop && !trace.retired && !trace.exception && cpu.state()==before,"noncanonical zero divide committed partial state");
    }
}
void guest() {
    System system;auto& memory=system.memory();
    memory.write_quadword(0x2000,{0xffffffff80000000ULL,53});
    const std::array program{0x78810000U,op(0,26,0,1,0),op(28,27,0,1,0),op(28,9,9,0,0,3),0x7c830010U,
        op(28,9,13,1,0),op(28,9,9,0,0,5),0x7c850020U,op(28,41,13,1,0),op(28,9,9,0,0,6),0x7c860030U,
        op(28,9,8,0,0,7),0x7c870040U};
    for(unsigned n=0;n<program.size();++n)memory.write(n*4U,4,program[n]);
    CpuState start;start.gpr[4].low=0x2000;system.cpu().restore(start);check(system.run(3)==RunResult{3,true},"zero divide guest setup");
    const auto saved=system.state();std::vector<InstructionTrace> first,second;check(system.run(10,&first)==RunResult{10,true},"zero divide guest run");
    check(memory.read_quadword(0x2010)==std::array<std::uint64_t,2>{1,~0ULL} &&
        memory.read_quadword(0x2020)==std::array<std::uint64_t,2>{1,~0ULL} &&
        memory.read_quadword(0x2030)==std::array<std::uint64_t,2>{~0ULL,~0ULL} &&
        memory.read_quadword(0x2040)==std::array<std::uint64_t,2>{0xffffffff80000000ULL,53},"zero divide guest RAM");
    const auto expected=system.state();system.restore(saved);system.run(3,&second);system.run(7,&second);
    check(system.state()==expected && first==second,"zero divide full-System replay");
}
}
int main() {
    try {zero_divisors();rejection_atomicity();guest();std::cout<<"Zero divide tests passed\n";}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
