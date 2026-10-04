#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace critterlink;
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
constexpr std::uint32_t mmi(unsigned sub,unsigned rs=1,unsigned rt=2,unsigned rd=3,unsigned fn=9) {
    return (28U<<26U)|(rs<<21U)|(rt<<16U)|(rd<<11U)|(sub<<6U)|fn;
}
using Words=std::array<std::uint32_t,4>;
Register128 pack(Words w) { return {std::uint64_t{w[0]}|(std::uint64_t{w[1]}<<32U),std::uint64_t{w[2]}|(std::uint64_t{w[3]}<<32U)}; }
Words unpack(Register128 r) { return {static_cast<std::uint32_t>(r.low),static_cast<std::uint32_t>(r.low>>32U),static_cast<std::uint32_t>(r.high),static_cast<std::uint32_t>(r.high>>32U)}; }
CpuState initial() {
    CpuState s; s.hi={0x7fffffff80000000ULL,0xffffffff00000000ULL};
    s.lo={0xffffffff00000000ULL,0x800000007fffffffULL}; s.sa=88;
    s.gpr[1]={0x0004000300020001ULL,0x0008000700060005ULL};
    s.gpr[2]={0xfffc0003fffe0001ULL,0xfff80007fffa0005ULL}; s.gpr[3]={42,43}; return s;
}
void run(Memory& memory,Cpu& cpu,std::uint32_t instruction,CpuState before,CpuState expected) {
    memory.write(0,4,instruction); cpu.restore(before); expected.pc=4; expected.next_pc=8;
    const auto trace=cpu.step(memory);
    check(trace.retired && !trace.stop && !trace.exception && cpu.state()==expected,"halfword result or preserved state");
}
CpuState oracle(CpuState before,unsigned sub,unsigned rs,unsigned rt,unsigned rd) {
    const auto a=unpack(before.gpr[rs]),b=unpack(before.gpr[rt]);
    auto lo=unpack(before.lo),hi=unpack(before.hi); Words output{};
    std::array<std::int64_t,8> product{};
    for(unsigned n=0;n<8;++n) {
        const auto x=(a[n/2]>>((n%2)*16))&65535U,y=(b[n/2]>>((n%2)*16))&65535U;
        const auto sx=x<32768?static_cast<std::int64_t>(x):static_cast<std::int64_t>(x)-65536;
        const auto sy=y<32768?static_cast<std::int64_t>(y):static_cast<std::int64_t>(y)-65536;
        product[n]=sx*sy;
    }
    // Explicit mapping of all eight product lanes to accumulator words.
    const std::array old{lo[0],lo[1],hi[0],hi[1],lo[2],lo[3],hi[2],hi[3]};
    std::array<std::uint32_t,8> result{};
    for(unsigned n=0;n<8;++n) {
        auto value=product[n];
        if(sub==16) value+=old[n];
        if(sub==20) value=static_cast<std::int64_t>(old[n])-value;
        if(sub==17 || sub==21) {
            if(n%2==0) value=sub==17?product[n]+product[n+1]:product[n+1]-product[n];
            else if(sub==21) value=-product[n]-1;
        }
        result[n]=static_cast<std::uint32_t>(value);
    }
    before.lo=pack({result[0],result[1],result[4],result[5]});
    before.hi=pack({result[2],result[3],result[6],result[7]});
    output={result[0],result[2],result[4],result[6]};
    if(rd!=0) before.gpr[rd]=pack(output);
    return before;
}
void products() {
    Memory memory; Cpu cpu;
    constexpr std::array values{0U,1U,2U,0x7fffU,0x8000U,0x8001U,0xfffeU,0xffffU};
    for(unsigned sub:{16U,17U,20U,21U,28U}) for(unsigned i=0;i<values.size();++i)
        for(unsigned j=0;j<values.size();++j) for(unsigned rd:{0U,1U,2U,3U}) {
            auto before=initial(); Words a{},b{};
            for(unsigned n=0;n<8;++n) {
                a[n/2]|=values[(i+n)%values.size()]<<((n%2)*16);
                b[n/2]|=values[(j+n*3)%values.size()]<<((n%2)*16);
            }
            before.gpr[1]=pack(a); before.gpr[2]=pack(b);
            run(memory,cpu,mmi(sub,1,2,rd),before,oracle(before,sub,1,2,rd));
        }
    for(unsigned sub:{16U,17U,20U,21U,28U})
        for(const auto sources:{std::array{0U,2U},std::array{1U,0U},std::array{0U,0U},std::array{1U,1U}})
            for(unsigned rd:{0U,1U,2U,3U}) {
                auto before=initial();run(memory,cpu,mmi(sub,sources[0],sources[1],rd),before,oracle(before,sub,sources[0],sources[1],rd));
            }
}
void hardware_vectors() {
    Memory memory; Cpu cpu;
    // Published ps2autotests C_P_S16_A/B, pinned in CPU coverage documentation.
    const auto a=pack({0x80008000U,0xffffffffU,0x12345678U,0x7fff7fffU});
    const auto b=pack({0x8000ffffU,0x12347fffU,0xffff8000U,0x7fff5678U});
    struct Vector { unsigned sub; Register128 hi,lo,rd; };
    const std::array vectors{
        Vector{17,{0xffffedccffff6dcdULL,0x3fff00016b3aa989ULL},{0x4000000040008000ULL,0xffffedccd4c3edccULL},{0xffff6dcd40008000ULL,0x6b3aa989d4c3edccULL}},
        Vector{21,{0x0000123300006dcbULL,0xc000fffe14c35679ULL},{0xbfffffff3fff8000ULL,0x000012332b3bedccULL},{0x00006dcb3fff8000ULL,0x14c356792b3bedccULL}}
    };
    for(const auto& v:vectors) {
        auto before=initial(); before.gpr[1]=a;before.gpr[2]=b;auto expected=before;
        expected.hi=v.hi;expected.lo=v.lo;expected.gpr[3]=v.rd;
        run(memory,cpu,mmi(v.sub),before,expected);
    }
    for(unsigned sub:{17U,21U}) {
        auto before=initial(); before.gpr[1]=before.gpr[2]={0x8000800080008000ULL,0x8000800080008000ULL};
        auto expected=before;
        const auto accumulator=sub==17?0x4000000080000000ULL:0xbfffffff00000000ULL;
        expected.hi=expected.lo={accumulator,accumulator};
        const auto result=sub==17?0x8000000080000000ULL:0ULL;
        expected.gpr[3]={result,result};
        run(memory,cpu,mmi(sub),before,expected);
    }
    // Literal independent lane outputs for PMULTH, including signed products.
    auto before=initial();auto expected=before;
    expected.lo={0xfffffffc00000001ULL,0xffffffdc00000019ULL};
    expected.hi={0xfffffff000000009ULL,0xffffffc000000031ULL};
    expected.gpr[3]={0x0000000900000001ULL,0x0000003100000019ULL};
    run(memory,cpu,mmi(28),before,expected);
}
void divide() {
    Memory memory;Cpu cpu;
    struct Vector { Words a; std::uint16_t divisor; Words q,r; };
    const std::array vectors{
        Vector{{7,0xfffffff9U,0x80000000U,0x7fffffff},3,{2,0xfffffffeU,0xd5555556U,0x2aaaaaaa},{1,0xffffffffU,0xfffffffeU,1}},
        Vector{{7,0xfffffff9U,0x80000000U,0x7fffffff},0xffff,{0xfffffff9U,7,0x80000000U,0x80000001U},{0,0,0,0}},
        Vector{{0,1,0xffffffffU,0x80000000U},0,{0xffffffffU,0xffffffffU,1,1},{0,1,0xffffffffU,0x80000000U}},
        Vector{{0xffff8000U,0xffff8001U,0x80000000U,0x7fffffff},0x8000,{1,0,65536,0xffff0001U},{0,0xffff8001U,0,32767}},
        Vector{{0x80008000U,0xffffffffU,0x12345678,0x7fff7fff},0xffff,{0x7fff8000,1,0xedcba988U,0x80008001U},{0,0,0,0}}
    };
    for(const auto& v:vectors) {
        auto before=initial(); before.gpr[1]=pack(v.a);before.gpr[2]={0x123456789abc0000ULL|v.divisor,~0ULL};
        auto expected=before;expected.lo=pack(v.q);expected.hi=pack(v.r);
        run(memory,cpu,mmi(29,1,2,0),before,expected);
    }
    // All nonzero halfword divisors: quotient/remainder identities and bounds.
    for(unsigned raw=1;raw<65536;++raw) {
        auto before=initial();before.gpr[1]=pack({0x80000000U,0xffffffffU,0,0x7fffffff});before.gpr[2].low=raw;
        memory.write(0,4,mmi(29,1,2,0));cpu.restore(before);check(cpu.step(memory).retired,"broadcast divisor rejected");
        const auto q=unpack(cpu.state().lo),r=unpack(cpu.state().hi);
        const auto divisor=raw<32768?static_cast<std::int64_t>(raw):static_cast<std::int64_t>(raw)-65536;
        constexpr std::array<std::int64_t,4> dividends{-2147483648LL,-1,0,2147483647};
        for(unsigned n=0;n<4;++n) {
            const auto quotient=q[n]<0x80000000U?static_cast<std::int64_t>(q[n]):static_cast<std::int64_t>(q[n])-0x100000000LL;
            const auto remainder=r[n]<0x80000000U?static_cast<std::int64_t>(r[n]):static_cast<std::int64_t>(r[n])-0x100000000LL;
            if(n==0 && divisor==-1) { check(q[n]==0x80000000U && r[n]==0,"broadcast overflow");continue; }
            const auto magnitude=remainder<0?-remainder:remainder,limit=divisor<0?-divisor:divisor;
            check(quotient*divisor+remainder==dividends[n] && magnitude<limit &&
                (remainder==0 || (remainder<0)==(dividends[n]<0)),"division identity/sign/range");
        }
    }
    for(const auto sources:{std::array{1U,1U},std::array{0U,2U},std::array{1U,0U}}) {
        auto before=initial();before.gpr[1]={0x0000000100000001ULL,0x0000000100000001ULL};before.gpr[2]={1,0};
        auto expected=before;expected.hi={};expected.lo=sources[0]==0?Register128{}:before.gpr[1];
        if(sources[1]==0) {expected.lo={~0ULL,~0ULL};expected.hi=before.gpr[1];}
        run(memory,cpu,mmi(29,sources[0],sources[1],0),before,expected);
    }
}
void invalid() {
    Memory memory;Cpu cpu;
    std::vector<std::uint32_t> opcodes;
    for(unsigned rd:{1U,2U,4U,8U,16U}) opcodes.push_back(mmi(29,1,2,rd));
    for(unsigned sub:{16U,17U,20U,21U,28U,29U}) opcodes.push_back(mmi(sub,1,2,3,41));
    for(auto op:opcodes) {
        auto before=initial();memory.write(0,4,op);cpu.restore(before);const auto trace=cpu.step(memory);before.stop=cpu.state().stop;
        check(trace.stop && !trace.retired && !trace.exception && cpu.state()==before,"reserved halfword encoding mutated state");
    }
}
void guest() {
    System system;auto& memory=system.memory();const auto state=initial();
    memory.write_quadword(0x2000,{state.gpr[1].low,state.gpr[1].high});memory.write_quadword(0x2010,{state.gpr[2].low,state.gpr[2].high});
    const std::array program{0x78810000U,0x78820010U,mmi(28),mmi(16),mmi(20),0x7c830020U,
        mmi(17),0x7c830030U,mmi(21),0x7c830040U,mmi(29,1,2,0),mmi(9,0,0,5),0x7c850050U};
    for(unsigned n=0;n<program.size();++n) memory.write(n*4U,4,program[n]);
    CpuState start;start.gpr[4].low=0x2000;system.cpu().restore(start);
    check(system.run(4)==RunResult{4,true},"halfword guest setup");const auto saved=system.state();
    std::vector<InstructionTrace> first,second;check(system.run(9,&first)==RunResult{9,true},"halfword guest execution");
    check(memory.read_quadword(0x2020)==std::array<std::uint64_t,2>{0x0000000900000001ULL,0x0000003100000019ULL} &&
        memory.read_quadword(0x2030)==std::array<std::uint64_t,2>{0xfffffff9fffffffdULL,0xfffffff1fffffff5ULL} &&
        memory.read_quadword(0x2040)==std::array<std::uint64_t,2>{0xffffffe7fffffffbULL,0xffffff8fffffffc3ULL} &&
        memory.read_quadword(0x2050)==std::array<std::uint64_t,2>{state.gpr[1].low,state.gpr[1].high},"halfword guest RAM output");
    const auto expected=system.state();system.restore(saved);system.run(2,&second);system.run(7,&second);
    check(system.state()==expected && first==second,"halfword full-System replay");
}
void delays() {
    Memory memory;Cpu cpu;
    for(unsigned sub:{16U,17U,20U,21U,28U,29U}) for(auto branch:{0x10000003U,0x14000003U,0x54000003U}) {
        memory.write(0,4,branch);memory.write(4,4,mmi(sub,1,2,sub==29?0:3));cpu.restore(initial());
        check(cpu.step(memory).retired,"halfword branch setup");
        if(branch==0x54000003U) check(cpu.state().pc==8 && cpu.state().gpr==initial().gpr && cpu.state().hi==initial().hi && cpu.state().lo==initial().lo,"annul mutated state");
        else {const auto saved=cpu.state();const auto trace=cpu.step(memory);const auto expected=cpu.state();cpu.restore(saved);
            check(cpu.step(memory)==trace && cpu.state()==expected && trace.retired && trace.delay_slot && expected.pc==(branch==0x10000003U?16U:8U),"halfword delay replay");}
    }
}
}
int main() {
    try {products();hardware_vectors();divide();invalid();guest();delays();std::cout<<"Packed halfword tests passed\n";}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
