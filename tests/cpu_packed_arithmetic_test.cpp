#include "critterlink/system.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace critterlink;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
enum class Kind { add, sub, signed_add, signed_sub, unsigned_add, unsigned_sub, equal, greater };
struct Spec { unsigned fn, sub, width; Kind kind; };
constexpr std::array specs{
    Spec{8,8,8,Kind::add}, Spec{8,9,8,Kind::sub}, Spec{8,24,8,Kind::signed_add}, Spec{8,25,8,Kind::signed_sub},
    Spec{40,24,8,Kind::unsigned_add}, Spec{40,25,8,Kind::unsigned_sub}, Spec{40,10,8,Kind::equal}, Spec{8,10,8,Kind::greater},
    Spec{8,4,16,Kind::add}, Spec{8,5,16,Kind::sub}, Spec{8,20,16,Kind::signed_add}, Spec{8,21,16,Kind::signed_sub},
    Spec{40,20,16,Kind::unsigned_add}, Spec{40,21,16,Kind::unsigned_sub}, Spec{40,6,16,Kind::equal}, Spec{8,6,16,Kind::greater},
    Spec{8,0,32,Kind::add}, Spec{8,1,32,Kind::sub}, Spec{8,16,32,Kind::signed_add}, Spec{8,17,32,Kind::signed_sub},
    Spec{40,16,32,Kind::unsigned_add}, Spec{40,17,32,Kind::unsigned_sub}, Spec{40,2,32,Kind::equal}, Spec{8,2,32,Kind::greater}
};
std::uint32_t opcode(Spec s, unsigned rs=1, unsigned rt=2, unsigned rd=3) {
    return (28U << 26U) | (rs << 21U) | (rt << 16U) | (rd << 11U) | (s.sub << 6U) | s.fn;
}
using Lanes = std::array<std::uint32_t,16>;
Register128 pack(const Lanes& values, unsigned width) {
    Register128 result;
    for (unsigned n=0; n<128U/width; ++n)
        (n*width<64 ? result.low : result.high) |= std::uint64_t{values[n]} << ((n*width)%64U);
    return result;
}
std::uint32_t oracle(Spec s, std::uint32_t a, std::uint32_t b) {
    const auto modulus = std::int64_t{1} << s.width, half = modulus/2;
    // Convert via modular offset, independently of the CPU sign-bit implementation.
    const auto x = (std::int64_t{a}+half)%modulus-half;
    const auto y = (std::int64_t{b}+half)%modulus-half;
    std::int64_t result = 0;
    switch (s.kind) {
    case Kind::add: result=std::int64_t{a}+b; break;
    case Kind::sub: result=std::int64_t{a}-b; break;
    case Kind::signed_add: result=std::clamp(x+y,-half,half-1); break;
    case Kind::signed_sub: result=std::clamp(x-y,-half,half-1); break;
    case Kind::unsigned_add: result=std::min(std::int64_t{a}+b,modulus-1); break;
    case Kind::unsigned_sub: result=std::max(std::int64_t{a}-b,std::int64_t{0}); break;
    case Kind::equal: result=a==b ? modulus-1 : 0; break;
    case Kind::greater: result=x>y ? modulus-1 : 0; break;
    }
    return static_cast<std::uint32_t>((result%modulus+modulus)%modulus);
}
void run(Memory& memory, Cpu& cpu, Spec s, Lanes a, Lanes b, unsigned rs=1, unsigned rt=2, unsigned rd=3) {
    CpuState initial; initial.gpr[1]=pack(a,s.width); initial.gpr[2]=pack(b,s.width);
    initial.gpr[3]={0xabcdef0123456789ULL,0xfedcba9876543210ULL};
    initial.hi={11,12}; initial.lo={13,14}; initial.sa=120;
    cpu.restore(initial); memory.write(0,4,opcode(s,rs,rt,rd));
    if (rs==0) a={};
    if (rt==0) b={};
    if (rs==rt) b=a;
    Lanes expected_lanes{};
    for (unsigned n=0; n<128U/s.width; ++n) expected_lanes[n]=oracle(s,a[n],b[n]);
    auto expected=initial; expected.pc=4; expected.next_pc=8;
    if (rd!=0) expected.gpr[rd]=pack(expected_lanes,s.width);
    const auto trace=cpu.step(memory);
    check(trace.retired && !trace.stop && !trace.exception && cpu.state()==expected,
          "packed arithmetic result, exception, alias or preserved-state mismatch");
}
void exhaustive_bytes() {
    Memory memory; Cpu cpu;
    for (const auto s: specs) {
        if (s.width!=8) continue;
        for (unsigned a=0; a<256; ++a) for (unsigned b=0; b<256; b+=16) {
            Lanes left{},right{};
            for (unsigned n=0; n<16; ++n) { left[n]=a; right[n]=b+n; }
            run(memory,cpu,s,left,right);
        }
    }
}
void wider_boundaries_and_aliases() {
    Memory memory; Cpu cpu;
    for (const auto s: specs) {
        const auto half=std::uint64_t{1}<<(s.width-1U), limit=half*2-1;
        const std::array<std::uint32_t,8> values{0,1,2,static_cast<std::uint32_t>(half-2),
            static_cast<std::uint32_t>(half-1),static_cast<std::uint32_t>(half),
            static_cast<std::uint32_t>(half+1),static_cast<std::uint32_t>(limit)};
        for (unsigned i=0; i<values.size(); ++i) for (unsigned j=0; j<values.size(); ++j) {
            Lanes left{},right{};
            for (unsigned n=0; n<128U/s.width; ++n) {
                left[n]=values[(i+n)%values.size()]; right[n]=values[(j+3*n)%values.size()];
            }
            for (unsigned rd: {0U,1U,2U,3U}) run(memory,cpu,s,left,right,1,2,rd);
            run(memory,cpu,s,left,right,0,2); run(memory,cpu,s,left,right,1,0);
            run(memory,cpu,s,left,right,1,1);
        }
    }
}
void literal_word_results() {
    Memory memory; Cpu cpu;
    const Lanes left{0x7fffffff,0x80000000,0xffffffff,0}, right{1,0xffffffff,1,1};
    constexpr std::array<Lanes,8> answers{
        Lanes{0x80000000,0x7fffffff,0,1}, Lanes{0x7ffffffe,0x80000001,0xfffffffe,0xffffffff},
        Lanes{0x7fffffff,0x80000000,0,1}, Lanes{0x7ffffffe,0x80000001,0xfffffffe,0xffffffff},
        Lanes{0x80000000,0xffffffff,0xffffffff,1}, Lanes{0x7ffffffe,0,0xfffffffe,0},
        Lanes{0,0,0,0}, Lanes{0xffffffff,0,0,0}};
    for (unsigned i=0; i<8; ++i) {
        CpuState state; state.gpr[1]=pack(left,32); state.gpr[2]=pack(right,32);
        cpu.restore(state); memory.write(0,4,opcode(specs[16+i]));
        check(cpu.step(memory).retired && cpu.state().gpr[3]==pack(answers[i],32), "literal word oracle failed");
    }
    // Signed subtraction overflow must clamp on both ends without trapping.
    CpuState state; state.gpr[1]=pack({0x7fffffff,0x80000000,0x80000000,0x7fffffff},32);
    state.gpr[2]=pack({0xffffffff,1,0x7fffffff,0x80000000},32); cpu.restore(state);
    memory.write(0,4,opcode(specs[19]));
    check(cpu.step(memory).retired && cpu.state().gpr[3]==pack({0x7fffffff,0x80000000,0x80000000,0x7fffffff},32),
          "signed subtract saturation endpoints");
}
void guest_and_delay_replay() {
    System system; auto& memory=system.memory();
    const auto left=pack({0x7fffffff,0x80000000,1,0xffffffff},32);
    const auto right=pack({1,0xffffffff,0xffffffff,0xffffffff},32);
    memory.write_quadword(0x2000,{left.low,left.high}); memory.write_quadword(0x2010,{right.low,right.high});
    const std::array program{0x78810000U,0x78820010U,opcode(specs[18]),
        opcode(specs[22],3,1,5),opcode(specs[23],3,0,6),0x7c830020U,0x7c850030U,0x7c860040U};
    for (unsigned n=0; n<program.size(); ++n) memory.write(n*4U,4,program[n]);
    CpuState state; state.gpr[4].low=0x2000; system.cpu().restore(state);
    check(system.run(3)==RunResult{3,true},"packed arithmetic guest setup");
    const auto checkpoint=system.state(); std::vector<InstructionTrace> first,second;
    check(system.run(5,&first)==RunResult{5,true},"packed arithmetic guest completion");
    check(memory.read_quadword(0x2020)==std::array<std::uint64_t,2>{0x800000007fffffffULL,0xfffffffe00000000ULL} &&
          memory.read_quadword(0x2030)==std::array<std::uint64_t,2>{0xffffffffffffffffULL,0} &&
          memory.read_quadword(0x2040)==std::array<std::uint64_t,2>{0x00000000ffffffffULL,0},"guest packed output");
    const auto expected=system.state(); system.restore(checkpoint);
    system.run(2,&second); system.run(3,&second);
    check(system.state()==expected && first==second,"packed arithmetic snapshot replay");
    for (const auto s: specs) {
        for (auto branch: {0x10000003U,0x14000003U,0x54000003U}) {
            memory.write(0,4,branch); memory.write(4,4,opcode(s));
            CpuState initial; initial.gpr[1]={~0ULL,~0ULL}; initial.gpr[2]={1,1}; system.cpu().restore(initial);
            check(system.cpu().step(memory).retired,"packed branch setup");
            if (branch==0x54000003U) {
                check(system.cpu().state().pc==8 && system.cpu().state().gpr==initial.gpr,"annulled arithmetic executed");
            } else {
                const auto saved=system.cpu().state(); const auto first_slot=system.cpu().step(memory);
                const auto after=system.cpu().state(); system.cpu().restore(saved);
                check(system.cpu().step(memory)==first_slot && first_slot.retired && first_slot.delay_slot &&
                      system.cpu().state()==after && after.pc==(branch==0x10000003U?16U:8U),"arithmetic delay replay");
            }
        }
    }
    // Unimplemented neighboring MMI operations must not fall through as arithmetic.
    for (const auto s: {Spec{8,11,8,Kind::add},Spec{8,12,32,Kind::add},Spec{40,0,32,Kind::add},Spec{40,8,8,Kind::add}}) {
        memory.write(0,4,opcode(s)); system.cpu().reset();
        check(system.cpu().step(memory).stop.has_value(),"unimplemented MMI neighbor accepted");
    }
}
}
int main() {
    try {
        exhaustive_bytes(); wider_boundaries_and_aliases(); literal_word_results(); guest_and_delay_replay();
        std::cout<<"Packed arithmetic tests passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
