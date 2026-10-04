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
    check(trace.retired && !trace.stop && !trace.exception && cpu.state()==expected,"packed accumulate/format result or preserved state");
}
void accumulates() {
    Memory memory; Cpu cpu;
    struct Vector { unsigned fn,sub; Register128 a,b,acc,result; };
    const std::array vectors{
        Vector{9,0,{7,extend(0xfffffff9U)},{3,2},{0,0},{21,0xfffffffffffffff2ULL}},
        Vector{9,0,{1,1},{1,1},{~0ULL,0x7fffffffffffffffULL},{0,0x8000000000000000ULL}},
        Vector{9,4,{1,extend(0xffffffffU)},{1,1},{0,0},{~0ULL,1}},
        Vector{9,4,{extend(0x80000000U),0x7fffffff},{extend(0x80000000U),0x7fffffff},
            {0,0},{0xc000000000000000ULL,0xc0000000ffffffffULL}},
        Vector{41,0,{~0ULL,extend(0x80000000U)},{~0ULL,2},{0x200000000ULL,~0ULL},{1,0xffffffffULL}},
        Vector{9,0,{extend(0x80000000U),extend(0x80000000U)},{~0ULL,1},
            {0xffffffffULL,0},{0x17fffffffULL,0xffffffff80000000ULL}}
    };
    for(const auto& v:vectors) for(unsigned rd:{0U,1U,2U,3U}) {
        auto before=initial(); before.gpr[1]=v.a; before.gpr[2]=v.b;
        // Upper words are deliberately unrelated to sign extension: they are ignored inputs.
        before.lo={0x1234567800000000ULL|(v.acc.low&0xffffffffULL),0x8765432100000000ULL|(v.acc.high&0xffffffffULL)};
        before.hi={0xdeadbeef00000000ULL|(v.acc.low>>32U),0xabcdef0100000000ULL|(v.acc.high>>32U)};
        auto expected=before; expected.lo={extend(static_cast<std::uint32_t>(v.result.low)),extend(static_cast<std::uint32_t>(v.result.high))};
        expected.hi={extend(static_cast<std::uint32_t>(v.result.low>>32U)),extend(static_cast<std::uint32_t>(v.result.high>>32U))};
        if(rd!=0) expected.gpr[rd]=v.result;
        run(memory,cpu,mmi(v.fn,v.sub,1,2,rd),before,expected);
    }
    for(auto op:{std::array{9U,0U},std::array{41U,0U},std::array{9U,4U}}) {
        auto before=initial(); before.hi={}; before.lo={}; before.gpr[1]={3,4};
        auto expected=before; const bool subtract=op[1]==4;
        expected.gpr[1]=subtract?Register128{0xfffffffffffffff7ULL,0xfffffffffffffff0ULL}:Register128{9,16};
        expected.lo=expected.gpr[1]; expected.hi=subtract?Register128{~0ULL,~0ULL}:Register128{};
        run(memory,cpu,mmi(op[0],op[1],1,1,1),before,expected);
        expected=before; expected.gpr[3]={}; run(memory,cpu,mmi(op[0],op[1],0,1),before,expected);
    }
}
void formats() {
    Memory memory; Cpu cpu;
    // Low-to-high bytes from a concatenation of LO followed by HI.
    constexpr std::array routes{
        std::array{0U,1U,2U,3U,16U,17U,18U,19U,8U,9U,10U,11U,24U,25U,26U,27U},
        std::array{4U,5U,6U,7U,20U,21U,22U,23U,12U,13U,14U,15U,28U,29U,30U,31U},
        std::array{0U,1U,4U,5U,16U,17U,20U,21U,8U,9U,12U,13U,24U,25U,28U,29U}
    };
    for(unsigned kind=0;kind<3;++kind) for(unsigned bit=0;bit<256;++bit) for(unsigned rd:{0U,1U,3U}) {
        auto before=initial(); before.hi={}; before.lo={};
        auto& source=bit<128?before.lo:before.hi;
        (bit%128<64?source.low:source.high)=std::uint64_t{1}<<(bit%64);
        auto expected=before; Register128 result;
        for(unsigned byte=0;byte<16;++byte) if(routes[kind][byte]==bit/8)
            (byte<8?result.low:result.high)|=std::uint64_t{1}<<((byte%8)*8+bit%8);
        if(rd!=0) expected.gpr[rd]=result;
        run(memory,cpu,mmi(48,kind==2?3:kind,0,0,rd),before,expected);
    }
    for(unsigned rs:{0U,1U,3U,31U}) {
        auto before=initial(); before.gpr[31]={0x1122334455667788ULL,0x99aabbccddeeff00ULL}; auto expected=before;
        const auto value=before.gpr[rs];
        expected.lo={0x0f1e2d3c00000000ULL|(value.low&0xffffffffULL),0x8877665500000000ULL|(value.high&0xffffffffULL)};
        expected.hi={0x1234567800000000ULL|(value.low>>32U),0xfedcba9800000000ULL|(value.high>>32U)};
        run(memory,cpu,mmi(49,0,rs,0,0),before,expected);
    }
}
void saturation() {
    Memory memory; Cpu cpu;
    struct Word { std::uint64_t input,output; };
    constexpr std::array words{
        Word{0,0},Word{1,1},Word{~0ULL,~0ULL},Word{0x7ffffffe,0x7ffffffe},Word{0x7fffffff,0x7fffffff},
        Word{0x80000000,0x7fffffff},Word{0x7fffffffffffffffULL,0x7fffffff},
        Word{0xffffffff80000001ULL,0xffffffff80000001ULL},Word{0xffffffff80000000ULL,0xffffffff80000000ULL},
        Word{0xffffffff7fffffffULL,0xffffffff80000000ULL},Word{0x8000000000000000ULL,0xffffffff80000000ULL}
    };
    for(const auto a:words) for(const auto b:words) for(unsigned rd:{0U,1U,3U}) {
        auto before=initial(); before.hi={0xdeadbeef00000000ULL|(a.input>>32U),0x7654321000000000ULL|(b.input>>32U)};
        before.lo={0x1234567800000000ULL|(a.input&0xffffffffULL),0xfedcba9800000000ULL|(b.input&0xffffffffULL)};
        auto expected=before; if(rd!=0) expected.gpr[rd]={a.output,b.output};
        run(memory,cpu,mmi(48,2,0,0,rd),before,expected);
    }
    struct Half { std::uint32_t input; unsigned output; };
    constexpr std::array halves{Half{0,0},Half{1,1},Half{0xffffffffU,0xffff},Half{0x7ffe,0x7ffe},
        Half{0x7fff,0x7fff},Half{0x8000,0x7fff},Half{0x7fffffff,0x7fff},Half{0x80000000,0x8000},
        Half{0xffff7fffU,0x8000},Half{0xffff8000U,0x8000},Half{0xffff8001U,0x8001}};
    for(unsigned offset=0;offset<halves.size();++offset) for(unsigned rd:{0U,1U,3U}) {
        auto before=initial(); before.hi={}; before.lo={}; Register128 result;
        for(unsigned n=0;n<8;++n) {
            const auto v=halves[(offset+n)%halves.size()]; auto& target=n%4<2?before.lo:before.hi;
            (n<4?target.low:target.high)|=std::uint64_t{v.input}<<((n%2)*32);
            (n<4?result.low:result.high)|=std::uint64_t{v.output}<<((n%4)*16);
        }
        auto expected=before; if(rd!=0) expected.gpr[rd]=result;
        run(memory,cpu,mmi(48,4,0,0,rd),before,expected);
    }
}
void reject(Memory& memory,Cpu& cpu,std::uint32_t instruction,CpuState before) {
    memory.write(0,4,instruction); cpu.restore(before); const auto trace=cpu.step(memory); before.stop=cpu.state().stop;
    check(trace.stop && !trace.retired && !trace.exception && cpu.state()==before,"invalid accumulate/format mutated state");
}
void invalid() {
    Memory memory; Cpu cpu;
    for(unsigned bit:{1U,2U,4U,8U,16U}) {
        for(unsigned fmt=0;fmt<5;++fmt) {
            reject(memory,cpu,mmi(48,fmt,bit,0),initial()); reject(memory,cpu,mmi(48,fmt,0,bit),initial());
        }
        reject(memory,cpu,mmi(49,0,1,bit,0),initial()); reject(memory,cpu,mmi(49,0,1,0,bit),initial());
    }
    for(unsigned fmt=0;fmt<32;++fmt) {
        if(fmt>4) reject(memory,cpu,mmi(48,fmt,0,0),initial());
        if(fmt!=0) reject(memory,cpu,mmi(49,fmt,1,0,0),initial());
    }
    for(auto op:{std::array{9U,0U},std::array{41U,0U},std::array{9U,4U}})
        for(unsigned rs:{1U,2U}) for(unsigned lane:{0U,1U}) for(unsigned rd:{0U,3U}) {
            auto before=initial(); (lane==0?before.gpr[rs].low:before.gpr[rs].high)=0x00000000ffffffffULL;
            reject(memory,cpu,mmi(op[0],op[1],1,2,rd),before);
        }
    reject(memory,cpu,mmi(41,4),initial());
}
void guest() {
    System system; auto& memory=system.memory();
    memory.write_quadword(0x2000,{7,0xfffffffffffffff9ULL}); memory.write_quadword(0x2010,{3,2});
    const std::array program{0x78810000U,0x78820010U,mmi(49,0,0,0,0),mmi(9,0),mmi(9,0),
        mmi(48,0,0,0,5),0x7c850020U,mmi(9,4),mmi(48,2,0,0,6),0x7c860030U};
    for(unsigned n=0;n<program.size();++n) memory.write(n*4U,4,program[n]);
    CpuState state; state.gpr[4].low=0x2000; system.cpu().restore(state);
    check(system.run(4)==RunResult{4,true},"accumulate guest setup"); const auto saved=system.state();
    std::vector<InstructionTrace> first,second; check(system.run(6,&first)==RunResult{6,true},"accumulate guest execution");
    check(memory.read_quadword(0x2020)==std::array<std::uint64_t,2>{42,0xffffffffffffffe4ULL} &&
        memory.read_quadword(0x2030)==std::array<std::uint64_t,2>{21,0xfffffffffffffff2ULL},"accumulate guest RAM output");
    const auto expected=system.state(); system.restore(saved); system.run(2,&second); system.run(4,&second);
    check(system.state()==expected && first==second,"accumulate full-System replay");
}
void delays() {
    Memory memory; Cpu cpu;
    for(auto instruction:{mmi(9,0),mmi(41,0),mmi(9,4),mmi(48,0,0,0),mmi(48,1,0,0),
        mmi(48,2,0,0),mmi(48,3,0,0),mmi(48,4,0,0),mmi(49,0,1,0,0)})
        for(auto branch:{0x10000003U,0x14000003U,0x54000003U}) {
            memory.write(0,4,branch); memory.write(4,4,instruction); cpu.restore(initial());
            check(cpu.step(memory).retired,"accumulate branch setup");
            if(branch==0x54000003U) check(cpu.state().pc==8 && cpu.state().gpr==initial().gpr &&
                cpu.state().hi==initial().hi && cpu.state().lo==initial().lo,"annul mutated accumulators");
            else {
                const auto saved=cpu.state(); const auto trace=cpu.step(memory); const auto expected=cpu.state(); cpu.restore(saved);
                check(cpu.step(memory)==trace && cpu.state()==expected && trace.retired && trace.delay_slot &&
                    expected.pc==(branch==0x10000003U?16U:8U),"accumulator delay replay");
            }
        }
}
}
int main() {
    try { accumulates(); formats(); saturation(); invalid(); guest(); delays(); std::cout<<"Packed accumulate tests passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
