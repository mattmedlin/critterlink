#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace critterlink;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr std::uint32_t mmi(unsigned fn, unsigned sub, unsigned rs=1, unsigned rt=2, unsigned rd=3) {
    return (28U<<26U)|(rs<<21U)|(rt<<16U)|(rd<<11U)|(sub<<6U)|fn;
}
std::uint64_t extend(std::uint32_t word) {
    return word < 0x80000000U ? word : 0xffffffff00000000ULL | word;
}
CpuState initial() {
    CpuState s; s.hi={0x123456789abcdef0ULL,0xfedcba9876543210ULL};
    s.lo={0x0f1e2d3c4b5a6978ULL,0x8877665544332211ULL}; s.sa=88;
    s.gpr[1]={7,extend(0xfffffff9U)}; s.gpr[2]={3,2}; s.gpr[3]={42,43}; return s;
}
void run(Memory& memory, Cpu& cpu, std::uint32_t instruction, CpuState before, CpuState expected) {
    memory.write(0,4,instruction); cpu.restore(before); expected.pc=4; expected.next_pc=8;
    const auto trace=cpu.step(memory);
    check(trace.retired && !trace.stop && !trace.exception && cpu.state()==expected,"packed HI/LO result or preserved state");
}
void moves() {
    Memory memory; Cpu cpu;
    for(unsigned sub:{8U,9U}) for(unsigned reg:{0U,1U,3U,31U}) {
        auto before=initial(); auto expected=before;
        if(reg!=0) expected.gpr[reg]=sub==8?before.hi:before.lo;
        run(memory,cpu,mmi(9,sub,0,0,reg),before,expected);
        before.gpr[31]={~0ULL,0x8000000000000001ULL}; expected=before;
        (sub==8?expected.hi:expected.lo)=before.gpr[reg];
        run(memory,cpu,mmi(41,sub,reg,0,0),before,expected);
    }
}
struct Multiply { unsigned fn; Register128 left,right,result,hi,lo; };
void multiply() {
    Memory memory; Cpu cpu;
    const std::array vectors{
        Multiply{9,{7,extend(0xfffffff9U)},{3,2},{21,0xfffffffffffffff2ULL},{0,~0ULL},{21,extend(0xfffffff2U)}},
        Multiply{9,{extend(0x80000000U),0x7fffffff},{extend(0x80000000U),0x7fffffff},
            {0x4000000000000000ULL,0x3fffffff00000001ULL},{0x40000000,0x3fffffff},{0,1}},
        Multiply{41,{~0ULL,extend(0x80000000U)},{~0ULL,2},
            {0xfffffffe00000001ULL,0x100000000ULL},{extend(0xfffffffeU),1},{1,0}},
        Multiply{41,{0,0x7fffffff},{~0ULL,2},{0,0xfffffffeULL},{0,0},{0,extend(0xfffffffeU)}}
    };
    for(const auto& v:vectors) for(unsigned rd:{0U,1U,2U,3U}) {
        auto before=initial(); before.gpr[1]=v.left; before.gpr[2]=v.right;
        auto expected=before; expected.hi=v.hi; expected.lo=v.lo;
        if(rd!=0) expected.gpr[rd]=v.result;
        run(memory,cpu,mmi(v.fn,12,1,2,rd),before,expected);
    }
    for(unsigned fn:{9U,41U}) {
        auto before=initial(); before.gpr[1]={3,4}; auto expected=before;
        expected.hi={}; expected.lo={9,16}; expected.gpr[1]={9,16};
        run(memory,cpu,mmi(fn,12,1,1,1),before,expected);
        expected=before; expected.hi={}; expected.lo={}; expected.gpr[3]={};
        run(memory,cpu,mmi(fn,12,0,1,3),before,expected);
    }
}
void divide() {
    Memory memory; Cpu cpu;
    struct Vector { unsigned fn; Register128 left,right,quotient,remainder; };
    const std::array vectors{
        Vector{9,{extend(0x80000000U),extend(0x80000000U)},{~0ULL,~0ULL},{extend(0x80000000U),extend(0x80000000U)},{0,0}},
        Vector{9,{7,extend(0xfffffff9U)},{3,2},{2,extend(0xfffffffdU)},{1,~0ULL}},
        Vector{9,{7,extend(0xfffffff9U)},{extend(0xfffffffdU),extend(0xfffffffdU)},
            {extend(0xfffffffeU),2},{1,~0ULL}},
        Vector{9,{extend(0x80000000U),0x7fffffff},{1,extend(0x80000000U)},
            {extend(0x80000000U),0},{0,0x7fffffff}},
        Vector{41,{~0ULL,extend(0x80000000U)},{1,3},{~0ULL,0x2aaaaaaa},{0,2}},
        Vector{41,{extend(0x80000000U),0},{~0ULL,1},{0,0},{extend(0x80000000U),0}}
    };
    for(const auto& v:vectors) {
        auto before=initial(); before.gpr[1]=v.left; before.gpr[2]=v.right;
        auto expected=before; expected.lo=v.quotient; expected.hi=v.remainder;
        run(memory,cpu,mmi(v.fn,13,1,2,0),before,expected);
    }
    for(unsigned fn:{9U,41U}) {
        auto before=initial(); auto expected=before; expected.lo={1,1}; expected.hi={};
        run(memory,cpu,mmi(fn,13,1,1,0),before,expected);
        expected=before; expected.lo={}; expected.hi={};
        run(memory,cpu,mmi(fn,13,0,2,0),before,expected);
    }
}
// Bit routing oracle, distinct from the interpreter's word-shift implementation.
std::uint64_t shifted(std::uint64_t data, unsigned amount, unsigned kind) {
    std::uint32_t word=0;
    for(unsigned bit=0;bit<32;++bit) {
        const int source=static_cast<int>(bit)+(kind==0?-static_cast<int>(amount):static_cast<int>(amount));
        const bool value=source>=0 && source<32 ? ((data>>source)&1U)!=0 :
            source>=32 && kind==2 && (data&0x80000000U)!=0;
        if(value) word|=1U<<bit;
    }
    return extend(word);
}
void shifts() {
    Memory memory; Cpu cpu;
    for(unsigned kind=0;kind<3;++kind) for(unsigned count=0;count<32;++count)
        for(auto data:{0ULL,1ULL,0x7fffffffULL,0x80000000ULL,0x80000001ULL,0xffffffffULL})
            for(unsigned rd:{0U,1U,2U,3U}) {
                auto before=initial(); before.gpr[1]={0xffffffe000000020ULL|count,0xabcdef0100000040ULL|(31U-count)};
                before.gpr[2]={0x1234567800000000ULL|data,0xfedcba9800000000ULL|(data^0xffffffffULL)};
                auto expected=before;
                if(rd!=0) expected.gpr[rd]={shifted(before.gpr[2].low,count,kind),shifted(before.gpr[2].high,31U-count,kind)};
                run(memory,cpu,mmi(kind==2?41:9,kind==0?2:3,1,2,rd),before,expected);
            }
    for(unsigned kind=0;kind<3;++kind) for(const auto sources:{std::array{0U,2U},std::array{1U,0U},std::array{1U,1U}}) {
        auto before=initial(); auto expected=before; const auto a=before.gpr[sources[0]],b=before.gpr[sources[1]];
        expected.gpr[1]={shifted(b.low,static_cast<unsigned>(a.low&31U),kind),shifted(b.high,static_cast<unsigned>(a.high&31U),kind)};
        run(memory,cpu,mmi(kind==2?41:9,kind==0?2:3,sources[0],sources[1],1),before,expected);
    }
}
void reject(Memory& memory, Cpu& cpu, std::uint32_t instruction, CpuState before) {
    memory.write(0,4,instruction); cpu.restore(before); const auto trace=cpu.step(memory);
    before.stop=cpu.state().stop;
    check(trace.stop && !trace.retired && !trace.exception && cpu.state()==before,"unsupported packed operation committed state");
}
void invalid() {
    Memory memory; Cpu cpu;
    for(unsigned bit:{1U,2U,4U,8U,16U}) for(unsigned sub:{8U,9U}) {
        reject(memory,cpu,mmi(9,sub,bit,0),initial());
        reject(memory,cpu,mmi(9,sub,0,bit),initial());
        reject(memory,cpu,mmi(41,sub,1,bit,0),initial());
        reject(memory,cpu,mmi(41,sub,1,0,bit),initial());
    }
    for(unsigned fn:{9U,41U}) for(unsigned lane:{0U,1U}) {
        for(unsigned sub:{12U,13U}) for(unsigned source:{1U,2U}) {
            auto before=initial(); (lane==0?before.gpr[source].low:before.gpr[source].high)=0x00000000ffffffffULL;
            reject(memory,cpu,mmi(fn,sub,1,2,sub==13?0:3),before);
        }
        auto before=initial(); (lane==0?before.gpr[2].low:before.gpr[2].high)=0;
        reject(memory,cpu,mmi(fn,13,1,2,0),before);
        for(unsigned rd:{1U,2U,4U,8U,16U}) reject(memory,cpu,mmi(fn,13,1,2,rd),initial());
    }
    reject(memory,cpu,mmi(41,2),initial());
}
void guest() {
    System system; auto& memory=system.memory();
    memory.write_quadword(0x2000,{7,0xfffffffffffffff9ULL}); memory.write_quadword(0x2010,{3,2});
    const std::array program{0x78810000U,0x78820010U,mmi(9,12),0x7c830020U,mmi(9,8,0,0,5),
        mmi(9,9,0,0,6),0x7c850030U,0x7c860040U,mmi(9,13,1,2,0),mmi(9,9,0,0,7),0x7c870050U};
    for(unsigned n=0;n<program.size();++n) memory.write(n*4U,4,program[n]);
    CpuState state; state.gpr[4].low=0x2000; system.cpu().restore(state);
    check(system.run(3)==RunResult{3,true},"HI/LO guest setup"); const auto saved=system.state();
    std::vector<InstructionTrace> first,second; check(system.run(8,&first)==RunResult{8,true},"HI/LO guest run");
    check(memory.read_quadword(0x2020)==std::array<std::uint64_t,2>{21,0xfffffffffffffff2ULL} &&
        memory.read_quadword(0x2030)==std::array<std::uint64_t,2>{0,~0ULL} &&
        memory.read_quadword(0x2040)==std::array<std::uint64_t,2>{21,0xfffffffffffffff2ULL} &&
        memory.read_quadword(0x2050)==std::array<std::uint64_t,2>{2,0xfffffffffffffffdULL},"HI/LO guest RAM results");
    const auto expected=system.state(); system.restore(saved); system.run(2,&second); system.run(6,&second);
    check(system.state()==expected && first==second,"HI/LO System replay");
}
void delays() {
    Memory memory; Cpu cpu;
    for(auto op:{mmi(9,8,0,0),mmi(9,9,0,0),mmi(41,8,1,0,0),mmi(41,9,1,0,0),
                 mmi(9,12),mmi(41,12),mmi(9,13,1,2,0),mmi(41,13,1,2,0),mmi(9,2),mmi(9,3),mmi(41,3)})
        for(auto branch:{0x10000003U,0x14000003U,0x54000003U}) {
            memory.write(0,4,branch); memory.write(4,4,op); cpu.restore(initial());
            check(cpu.step(memory).retired,"HI/LO branch setup");
            if(branch==0x54000003U) check(cpu.state().pc==8 && cpu.state().gpr==initial().gpr &&
                cpu.state().hi==initial().hi && cpu.state().lo==initial().lo,"annul mutated state");
            else {
                const auto saved=cpu.state(); const auto trace=cpu.step(memory); const auto expected=cpu.state();
                cpu.restore(saved); check(cpu.step(memory)==trace && cpu.state()==expected && trace.retired && trace.delay_slot &&
                    expected.pc==(branch==0x10000003U?16U:8U),"packed delay replay");
            }
        }
}
}
int main() {
    try { moves(); multiply(); divide(); shifts(); invalid(); guest(); delays(); std::cout<<"Packed HI/LO tests passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
