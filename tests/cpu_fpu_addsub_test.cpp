#include "critterlink/system.hpp"
#include "cpu_fpu_addsub_vectors.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using namespace critterlink;
void check(bool v,const char* message) { if (!v) throw std::runtime_error(message); }
constexpr std::uint32_t op(unsigned fn,unsigned fs,unsigned ft,unsigned fd=0) {
    return 0x46000000U|(ft<<16U)|(fs<<11U)|(fd<<6U)|fn;
}
CpuState initial() {
    CpuState s; s.cop0.status=0x20000000; s.fpu.control=0x01830061;
    s.fpu.accumulator=0xdeadbeef;
    for (unsigned i=1;i<32;++i) s.gpr[i]={i,~std::uint64_t{i}};
    return s;
}
void run(Memory& m,Cpu& cpu,unsigned fn,std::uint32_t a,std::uint32_t b,
         std::uint32_t bits,std::uint32_t flags,unsigned fd=2) {
    auto s=initial(); s.fpu.fpr[0]=a; s.fpu.fpr[31]=b;
    auto expected=s; expected.pc=4; expected.next_pc=8; expected.fpu.control|=flags;
    if(fn>=24) expected.fpu.accumulator=bits; else expected.fpu.fpr[fd]=bits;
    m.write(0,4,op(fn,0,31,fn>=24?0:fd)); cpu.restore(s); const auto t=cpu.step(m);
    check(t.retired&&!t.stop&&!t.exception&&cpu.state()==expected,"FPU add/sub result, flags or preserved state");
}
void vectors() {
    Memory m; Cpu cpu;
    for(const auto& v:fpu_addsub_vectors) for(unsigned acc:{0U,24U})
        for(unsigned fd:{0U,2U,31U}) run(m,cpu,v[0]+acc,v[1],v[2],v[3],v[4],fd);
    // Exact arithmetic, underflow, overflow and near-boundary cancellation.
    constexpr std::array<std::array<std::uint32_t,5>,16> cases{{
        {0,0x00800000,0x00800000,0x01000000,0},
        {1,0x00800001,0x00800000,0,0x4008},
        {1,0x00800000,0x00800001,0x80000000,0x4008},
        {1,0x00800000,0x00800000,0,0},
        {1,0x01000000,0x00800000,0x00800000,0},
        {0,0x7fffffff,0x7fffffff,0x7fffffff,0x8010},
        {1,0xffffffff,0x7fffffff,0xffffffff,0x8010},
        {0,1,0x80000001,0,0},
        {0,0x3f800000,0x3f800000,0x40000000,0},
        {1,0x3f800001,0x3f800000,0x34000000,0},
        // One retained alignment bit: exponent distance 24 survives; 25 drops.
        {1,0x3f800000,0x33800000,0x3f7fffff,0},
        {0,0x3f800000,0x33800000,0x3f800000,0},
        {1,0x3f800000,0x33000000,0x3f800000,0},
        {1,0x3f800000,0x33c00000,0x3f7fffff,0},
        {1,0xbf800000,0xb3800000,0xbf7fffff,0},
        {1,0xbf800000,0xb3000000,0xbf800000,0}}};
    for(const auto& v:cases) for(unsigned acc:{0U,24U}) run(m,cpu,v[0]+acc,v[1],v[2],v[3],v[4]);
    // Explicit current-flag clearing and sticky accumulation across instructions.
    auto s=initial();s.fpu.control=0x0183c079;s.fpu.fpr[0]=0x3f800000;
    m.write(0,4,op(0,0,0,1));cpu.restore(s);auto expected=s;
    expected.pc=4;expected.next_pc=8;expected.fpu.fpr[1]=0x40000000;expected.fpu.control=0x01830079;
    check(cpu.step(m).retired&&cpu.state()==expected,"arithmetic current/sticky flag distinction");
    // Every source/destination register and all aliases.
    for(unsigned fs=0;fs<32;++fs)for(unsigned ft=0;ft<32;++ft)for(unsigned fd:{0U,fs,ft,31U}) {
        s=initial();s.fpu.fpr[fs]=0x40000000;s.fpu.fpr[ft]=0x3f800000;expected=s;
        expected.pc=4;expected.next_pc=8;expected.fpu.fpr[fd]=fs==ft?0x40000000U:0x40400000U;
        m.write(0,4,op(0,fs,ft,fd));cpu.restore(s);
        check(cpu.step(m).retired&&cpu.state()==expected,"add register addressing");
    }
}
void control_flow() {
    Memory m;Cpu cpu;
    for(unsigned fn:{0U,1U,24U,25U}) {
        for(bool slot:{false,true}) {
            auto s=initial();s.cop0.status=0;s.pc=slot?4U:0U;s.next_pc=8;s.delay_slot=slot;
            m.write(s.pc,4,op(fn,0,31));cpu.restore(s);const auto t=cpu.step(m);
            check(t.exception==11U&&!t.retired&&!t.stop&&cpu.state().fpu==s.fpu&&
                  cpu.state().cop0.cause==(slot?0x9000002cU:0x1000002cU),"add/sub CU1");
        }
        for(auto branch:{0x10000003U,0x14000003U,0x54000003U}) {
            auto s=initial();s.fpu.fpr[0]=0x3f800000;s.fpu.fpr[31]=0x3f800000;
            m.write(0,4,branch);m.write(4,4,op(fn,0,31));cpu.restore(s);
            check(cpu.step(m).retired,"add/sub branch setup");
            if(branch==0x54000003U)check(cpu.state().pc==8&&cpu.state().fpu==s.fpu,"add/sub annul");
            else {const auto saved=cpu.state();const auto t=cpu.step(m);const auto expected=cpu.state();cpu.restore(saved);
                check(t.retired&&t.delay_slot&&cpu.step(m)==t&&cpu.state()==expected,"add/sub slot replay");}
        }
    }
    for(unsigned fn:{24U,25U})for(unsigned fd=1;fd<32;++fd) {
        auto s=initial();m.write(0,4,op(fn,0,31,fd));cpu.restore(s);const auto t=cpu.step(m);s.stop=cpu.state().stop;
        check(t.stop&&!t.retired&&!t.exception&&cpu.state()==s,"reserved accumulator destination");
    }
}
void guest() {
    System system;auto& m=system.memory();
    const std::array<std::uint32_t,7> program{op(24,0,1),op(25,1,0),op(0,0,1,2),op(1,1,0,3),0xe4020400,0xe4030404,0};
    for(std::size_t i=0;i<program.size();++i)m.write(static_cast<std::uint32_t>(i*4),4,program[i]);
    auto s=initial();s.fpu.fpr[0]=0x3f800000;s.fpu.fpr[1]=0x40400000;system.cpu().restore(s);
    check(system.run(1)==RunResult{1,true}&&system.cpu().state().fpu.accumulator==0x40800000,"ADDA guest result");
    const auto saved=system.state();std::vector<InstructionTrace>a,b;
    check(system.run(5,&a)==RunResult{5,true}&&m.read(0x400,4)==0x40800000U&&m.read(0x404,4)==0x40000000U&&
          system.cpu().state().fpu.accumulator==0x40000000,"add/sub guest RAM and ACC");
    const auto expected=system.state();system.restore(saved);system.run(2,&b);system.run(3,&b);
    check(system.state()==expected&&a==b,"add/sub full System replay");
}
}
int main(){try{vectors();control_flow();guest();std::cout<<"FPU add/sub tests passed\n";}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
