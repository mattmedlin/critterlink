#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace critterlink;
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
constexpr std::uint32_t mmi(unsigned fn,unsigned sub,unsigned rs=1,unsigned rt=2,unsigned rd=3) {
    return (28U<<26U)|(rs<<21U)|(rt<<16U)|(rd<<11U)|(sub<<6U)|fn;
}
using Bytes=std::array<unsigned,16>;
Register128 pack(Bytes bytes) {
    Register128 r;
    for(unsigned n=0;n<16;++n) (n<8?r.low:r.high)|=std::uint64_t{bytes[n]}<<((n%8)*8U);
    return r;
}
Bytes unpack(Register128 r) {
    Bytes bytes{};
    for(unsigned n=0;n<16;++n) bytes[n]=static_cast<unsigned>(((n<8?r.low:r.high)>>((n%8)*8U))&255U);
    return bytes;
}
struct Permutation { unsigned fn,sub; bool unary; Bytes source; };
// Explicit low-to-high byte routing: 0..15 select rt, 16..31 select rs.
constexpr std::array permutations{
    Permutation{8,26,false,{0,16,1,17,2,18,3,19,4,20,5,21,6,22,7,23}},
    Permutation{8,22,false,{0,1,16,17,2,3,18,19,4,5,20,21,6,7,22,23}},
    Permutation{8,18,false,{0,1,2,3,16,17,18,19,4,5,6,7,20,21,22,23}},
    Permutation{40,26,false,{8,24,9,25,10,26,11,27,12,28,13,29,14,30,15,31}},
    Permutation{40,22,false,{8,9,24,25,10,11,26,27,12,13,28,29,14,15,30,31}},
    Permutation{40,18,false,{8,9,10,11,24,25,26,27,12,13,14,15,28,29,30,31}},
    Permutation{8,27,false,{0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30}},
    Permutation{8,23,false,{0,1,4,5,8,9,12,13,16,17,20,21,24,25,28,29}},
    Permutation{8,19,false,{0,1,2,3,8,9,10,11,16,17,18,19,24,25,26,27}},
    Permutation{9,14,false,{0,1,2,3,4,5,6,7,16,17,18,19,20,21,22,23}},
    Permutation{41,14,false,{24,25,26,27,28,29,30,31,8,9,10,11,12,13,14,15}},
    Permutation{41,27,true,{0,1,0,1,0,1,0,1,8,9,8,9,8,9,8,9}},
    Permutation{9,10,false,{0,1,24,25,2,3,26,27,4,5,28,29,6,7,30,31}},
    Permutation{41,10,false,{0,1,16,17,4,5,20,21,8,9,24,25,12,13,28,29}},
    Permutation{9,26,true,{4,5,2,3,0,1,6,7,12,13,10,11,8,9,14,15}},
    Permutation{41,26,true,{0,1,4,5,2,3,6,7,8,9,12,13,10,11,14,15}},
    Permutation{9,30,true,{8,9,10,11,4,5,6,7,0,1,2,3,12,13,14,15}},
    Permutation{41,30,true,{0,1,2,3,8,9,10,11,4,5,6,7,12,13,14,15}},
    Permutation{9,27,true,{6,7,4,5,2,3,0,1,14,15,12,13,10,11,8,9}},
    Permutation{9,31,true,{4,5,6,7,8,9,10,11,0,1,2,3,12,13,14,15}}
};
void run(Memory& memory,Cpu& cpu,std::uint32_t opcode,CpuState state,unsigned rd,Register128 result) {
    memory.write(0,4,opcode); cpu.restore(state);
    auto expected=state; expected.pc=4; expected.next_pc=8;
    if(rd!=0) expected.gpr[rd]=result;
    const auto trace=cpu.step(memory);
    check(trace.retired && !trace.stop && !trace.exception && cpu.state()==expected,"packed result or preserved-state mismatch");
}
CpuState initial() {
    CpuState s; s.hi={11,12}; s.lo={13,14}; s.sa=120;
    s.gpr[3]={~0ULL,~0ULL}; return s;
}
void routes() {
    Memory memory; Cpu cpu;
    for(const auto& p:permutations) {
        // Label bytes, then exercise every source bit independently.
        for(unsigned sample=0;sample<=256;++sample) {
            Bytes left{},right{};
            for(unsigned n=0;n<16;++n) {
                if(sample==256) { right[n]=n; left[n]=n+16; }
                else if(sample/8==n) right[n]=1U<<(sample%8);
                else if(sample/8==n+16) left[n]=1U<<(sample%8);
            }
            auto state=initial(); state.gpr[1]=pack(left); state.gpr[2]=pack(right);
            Bytes expected{};
            for(unsigned n=0;n<16;++n) expected[n]=p.source[n]<16?right[p.source[n]]:left[p.source[n]-16];
            for(unsigned rd:{0U,1U,2U,3U}) run(memory,cpu,mmi(p.fn,p.sub,p.unary?0:1,2,rd),state,rd,pack(expected));
        }
        for(const auto sources:{std::array{0U,2U},std::array{0U,0U},std::array{1U,0U},std::array{1U,1U}}) {
            if(p.unary && sources[0]!=0) continue;
            auto state=initial(); state.gpr[1]={0x1122334455667788ULL,0x99aabbccddeeff00ULL};
            state.gpr[2]={0xfedcba9876543210ULL,0x0123456789abcdefULL};
            const auto a=unpack(state.gpr[sources[0]]),b=unpack(state.gpr[sources[1]]); Bytes expected{};
            for(unsigned n=0;n<16;++n) expected[n]=p.source[n]<16?b[p.source[n]]:a[p.source[n]-16];
            run(memory,cpu,mmi(p.fn,p.sub,sources[0],sources[1]),state,3,pack(expected));
        }
    }
}
void color_formats() {
    Memory memory; Cpu cpu;
    // All 16-bit inputs, four distinct words per instruction, with ignored high bits set.
    for(unsigned base=0;base<65536;base+=4) {
        Bytes input{},expected{},packed{};
        for(unsigned lane=0;lane<4;++lane) {
            const unsigned v=base+lane;
            input[lane*4]=v%256; input[lane*4+1]=v/256; input[lane*4+2]=0xa5; input[lane*4+3]=0x5a;
            expected[lane*4]=(v%32)*8; expected[lane*4+1]=((v/32)%32)*8;
            expected[lane*4+2]=((v/1024)%32)*8; expected[lane*4+3]=(v/32768)*128;
            packed[lane*4]=v%256; packed[lane*4+1]=v/256;
        }
        auto state=initial(); state.gpr[2]=pack(input);
        run(memory,cpu,mmi(8,30,0,2,2),state,2,pack(expected));
        state=initial(); state.gpr[2]=pack(expected);
        run(memory,cpu,mmi(8,31,0,2,2),state,2,pack(packed));
    }
    // PPAC5 selects these exact source bits; every other bit must disappear.
    constexpr std::array selected{3U,4U,5U,6U,7U,11U,12U,13U,14U,15U,19U,20U,21U,22U,23U,31U};
    for(unsigned bit=0;bit<128;++bit) {
        auto state=initial(); (bit<64?state.gpr[2].low:state.gpr[2].high)=std::uint64_t{1}<<(bit%64);
        Register128 expected;
        for(unsigned n=0;n<selected.size();++n) if(selected[n]==bit%32)
            ((bit/32)<2?expected.low:expected.high)|=std::uint64_t{1}<<(((bit/32)%2)*32+n);
        for(unsigned rd:{0U,2U,3U}) run(memory,cpu,mmi(8,31,0,2,rd),state,rd,expected);
    }
}
void arithmetic() {
    Memory memory; Cpu cpu;
    for(unsigned width:{16U,32U}) {
        const std::uint64_t sign=std::uint64_t{1}<<(width-1),mask=sign*2-1;
        const std::array values{std::uint64_t{0},std::uint64_t{1},sign-1,sign,sign+1,mask};
        for(auto a:values) for(auto b:values) {
            for(unsigned kind=0;kind<3;++kind) {
                auto state=initial(); Register128 result;
                for(unsigned lane=0;lane<128/width;++lane) {
                    const auto x=lane%2?a:b,y=lane%2?b:a;
                    (lane*width<64?state.gpr[1].low:state.gpr[1].high)|=x<<((lane*width)%64);
                    (lane*width<64?state.gpr[2].low:state.gpr[2].high)|=y<<((lane*width)%64);
                    const auto signed_x=static_cast<std::int64_t>((x+sign)%(mask+1))-static_cast<std::int64_t>(sign);
                    const auto signed_y=static_cast<std::int64_t>((y+sign)%(mask+1))-static_cast<std::int64_t>(sign);
                    const auto expected=kind==0?(signed_x>signed_y?x:y):kind==1?(signed_x<signed_y?x:y):
                        y==sign?sign-1:signed_y<0?static_cast<std::uint64_t>(-signed_y):y;
                    (lane*width<64?result.low:result.high)|=expected<<((lane*width)%64);
                }
                const unsigned fn=kind==0?8U:40U,sub=(kind==2?1U:3U)+(width==16?4U:0U);
                for(unsigned rd:{0U,1U,2U,3U}) run(memory,cpu,mmi(fn,sub,kind==2?0:1,2,rd),state,rd,result);
            }
        }
    }
    // Independent PADSBH wrap and lane-boundary expectations.
    auto state=initial(); state.gpr[1]={0x800000007fffffffULL,0xffff80007fff0000ULL};
    state.gpr[2]={0x00010001ffff0001ULL,0x0001ffff0001ffffULL};
    for(unsigned rd:{0U,1U,2U,3U}) run(memory,cpu,mmi(40,4,1,2,rd),state,rd,
        {0x7fffffff8000fffeULL,0x00007fff8000ffffULL});
}
void reserved_fields() {
    Memory memory; Cpu cpu;
    std::vector<std::uint32_t> instructions{mmi(40,1,0),mmi(40,5,0),mmi(8,30,0),mmi(8,31,0)};
    for(const auto& p:permutations) if(p.unary) instructions.push_back(mmi(p.fn,p.sub,0));
    for(auto instruction:instructions) for(unsigned rs:{1U,2U,4U,8U,16U}) {
        auto state=initial(); memory.write(0,4,instruction|(rs<<21U)); cpu.restore(state);
        const auto trace=cpu.step(memory); state.stop=cpu.state().stop;
        check(trace.stop && !trace.retired && !trace.exception && cpu.state()==state,"unary reserved rs accepted or changed state");
    }
}
void guest_replay() {
    System system; auto& memory=system.memory();
    memory.write_quadword(0x2000,{0x0706050403020100ULL,0x0f0e0d0c0b0a0908ULL});
    memory.write_quadword(0x2010,{0x1716151413121110ULL,0x1f1e1d1c1b1a1918ULL});
    const std::array program{0x78810000U,0x78820010U,mmi(8,26,1,2,3),mmi(40,26,1,2,5),
        mmi(8,27,5,3,6),0x7c860020U};
    for(unsigned n=0;n<program.size();++n) memory.write(n*4U,4,program[n]);
    CpuState state; state.gpr[4].low=0x2000; system.cpu().restore(state);
    check(system.run(3)==RunResult{3,true},"permutation guest setup");
    const auto checkpoint=system.state(); std::vector<InstructionTrace> first,second;
    check(system.run(3,&first)==RunResult{3,true} && memory.read_quadword(0x2020)==
        std::array<std::uint64_t,2>{0x1716151413121110ULL,0x1f1e1d1c1b1a1918ULL},"interleave/pack guest output");
    const auto expected=system.state(); system.restore(checkpoint); system.run(1,&second); system.run(2,&second);
    check(system.state()==expected && first==second,"permutation System replay");
    std::vector<std::uint32_t> instructions{mmi(8,3),mmi(8,7),mmi(40,3),mmi(40,7),mmi(40,1,0),mmi(40,5,0),mmi(40,4),mmi(8,30,0),mmi(8,31,0)};
    for(const auto& p:permutations) instructions.push_back(mmi(p.fn,p.sub,p.unary?0:1));
    for(auto instruction:instructions) for(auto branch:{0x10000003U,0x14000003U,0x54000003U}) {
        memory.write(0,4,branch); memory.write(4,4,instruction); system.cpu().restore(initial());
        check(system.cpu().step(memory).retired,"permutation branch setup");
        if(branch==0x54000003U) check(system.cpu().state().pc==8 && system.cpu().state().gpr==initial().gpr,"annulled instruction executed");
        else {
            const auto saved=system.cpu().state(); const auto trace=system.cpu().step(memory); const auto after=system.cpu().state();
            system.cpu().restore(saved);
            check(system.cpu().step(memory)==trace && system.cpu().state()==after && trace.retired && trace.delay_slot &&
                after.pc==(branch==0x10000003U?16U:8U),"delay slot replay");
        }
    }
}
}
int main() {
    try { routes(); color_formats(); arithmetic(); reserved_fields(); guest_replay(); std::cout<<"Packed permutation tests passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
