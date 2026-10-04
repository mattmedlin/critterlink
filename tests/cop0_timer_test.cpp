#include "critterlink/system.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace critterlink;
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
constexpr std::uint32_t move(unsigned mode,unsigned rt,unsigned rd) {
    return 0x40000000U|(mode<<21U)|(rt<<16U)|(rd<<11U);
}
void clock_matches() {
    Cpu cpu;auto s=cpu.state();s.cop0.count=0xfffffff0;s.cop0.compare=0x10;s.cop0.cause=0x30000424;cpu.restore(s);
    cpu.advance_cycles(31);check(cpu.state().cop0.count==0xf && cpu.state().cop0.cause==0x30000424,"wrap before match");
    cpu.advance_cycles(1);check(cpu.state().cop0.count==0x10 && cpu.state().cop0.cause==0x30008424,"wrap match latches IP7 without clobber");
    cpu.advance_cycles(7);check(cpu.state().cop0.count==0x17 && (cpu.state().cop0.cause&0x8000U)!=0,"pending remains after equality");
    s=CpuState{};s.cop0.count=123;s.cop0.compare=123;cpu.restore(s);cpu.advance_cycles(0);
    check(cpu.state()==s,"zero cycles do not trigger initial equality");
    cpu.advance_cycles(0xffffffffULL);check(cpu.state().cop0.count==122 && cpu.state().cop0.cause==0,"equal starting values wait for wrap");
    cpu.advance_cycles(1);check(cpu.state().cop0.count==123 && cpu.state().cop0.cause==0x8000,"full period match");
    cpu.restore(s);cpu.advance_cycles(std::numeric_limits<std::uint64_t>::max());
    check(cpu.state().cop0.count==122 && cpu.state().cop0.cause==0x8000,"64-bit advance does not skip match or overflow");
    cpu.restore(s);cpu.advance_cycles((1ULL<<32)+17);const auto whole=cpu.state();cpu.restore(s);
    cpu.advance_cycles(1ULL<<31);cpu.advance_cycles((1ULL<<31)+17);check(cpu.state()==whole,"clock advancement partition independence");
}
void transfers() {
    Memory m;Cpu cpu;
    for(unsigned reg:{9U,11U}) for(auto value:{0U,0x80000000U,0xffffffffU}) {
        auto s=cpu.state();s.cop0.count=4;s.cop0.compare=5;s.cop0.cause=0x8000;s.gpr[1]={0x1234567800000000ULL|value,0xabcdef};
        m.write(0,4,move(4,1,reg));s.pc=0;s.next_pc=4;cpu.restore(s);check(cpu.step(m).retired,"timer register write");
        check((reg==9?cpu.state().cop0.count:cpu.state().cop0.compare)==value &&
              cpu.state().cop0.cause==(reg==9?0x8000U:0U),"low-word write and Compare-only acknowledgement");
        s=cpu.state();s.pc=0;s.next_pc=4;s.gpr[2].high=0x76543210;m.write(0,4,move(0,2,reg));cpu.restore(s);
        check(cpu.step(m).retired && cpu.state().gpr[2]==Register128{value<0x80000000U?value:0xffffffff00000000ULL|value,0x76543210},"timer MFC0 sign extension and upper lane");
    }
    auto s=CpuState{};s.cop0.count=4;s.cop0.compare=5;s.cop0.cause=0x8000;m.write(0,4,move(4,0,11));cpu.restore(s);
    check(cpu.step(m).retired && cpu.state().cop0.compare==0 && cpu.state().cop0.cause==0,"Compare write r0 clears pending");
    s=cpu.state();s.pc=0;s.next_pc=4;m.write(0,4,move(0,0,9));cpu.restore(s);
    check(cpu.step(m).retired && cpu.state().gpr[0]==Register128{} && cpu.state().cop0.count==4,"Count read r0 and untimed step");
    for(unsigned reg:{9U,11U}) for(unsigned mode:{0U,4U}) {
        s=CpuState{};s.cop0.status=0x10;m.write(0,4,move(mode,1,reg));cpu.restore(s);
        check(cpu.step(m).exception==11U,"timer transfers require CU0 outside kernel");
    }
    cpu.reset_boot_vector();check(cpu.state().cop0.count==0 && cpu.state().cop0.compare==0 && (cpu.state().cop0.cause&0x8000U)==0,"deterministic reset timer state");
}
void enable_disable() {
    Memory m;Cpu cpu;
    for(auto ksu:{0U,8U,16U}) for(auto level:{0U,2U,4U,6U}) for(auto edi:{0U,0x20000U})
    for(auto cu0:{0U,0x10000000U}) for(auto eie:{0U,0x10000U}) for(auto instruction:{0x42000038U,0x42000039U}) {
        auto s=CpuState{};s.cop0.status=ksu|level|edi|cu0|eie|1U;s.gpr[1]={12,34};m.write(0,4,instruction);cpu.restore(s);
        auto expected=s;expected.pc=4;expected.next_pc=8;
        if(ksu==0 || level!=0 || edi!=0) expected.cop0.status=(s.cop0.status&~0x10000U)|(instruction==0x42000038U?0x10000U:0U);
        check(cpu.step(m).retired && cpu.state()==expected,"EI/DI EDI privilege matrix including CU0 clear");
    }
    // EDI is guest writable; EI itself works from mapped user code with CU0 clear.
    auto s=CpuState{};s.architectural_memory=true;s.pc=0x00400000;s.next_pc=s.pc+4;s.cop0.status=0x20010;
    s.mmu.entries[0]={0,0x00400000,0x17,0x57};m.write(0,4,0x42000038);cpu.restore(s);
    check(cpu.step(m).retired && cpu.state().cop0.status==0x30010,"mapped user EI does not spuriously fault CU0");
    s=CpuState{};s.gpr[1].low=0x78c01;m.write(0,4,move(4,1,12));m.write(4,4,move(0,2,12));cpu.restore(s);
    check(cpu.step(m).retired && cpu.step(m).retired && cpu.state().gpr[2].low==0x78c01,"Status IM7/EDI/CH transfers");
    for(auto bit:{6U,16U,20U}) {
        m.write(0,4,0x42000038U|(1U<<bit));cpu.reset();check(cpu.step(m).stop.has_value(),"noncanonical EI encoding must stop");
    }
}
void interrupt_gates() {
    Memory m;Cpu cpu;m.write(0,4,0);m.write(4,4,0x42000038);
    for(auto status:{0U,0x8001U,0x18000U,0x10001U,0x18003U,0x18005U}) {
        auto s=CpuState{};s.cop0.status=status;s.cop0.count=4;s.cop0.compare=5;cpu.restore(s);cpu.advance_cycles(1);
        check(cpu.step(m).retired && (cpu.state().cop0.cause&0x8000U)!=0,"masked timer pending stays readable");
    }
    for(bool bev:{false,true}) for(bool slot:{false,true}) {
        auto s=CpuState{};s.cop0.status=0x18001U|(bev?0x400000U:0U);s.cop0.cause=0x8000;
        s.pc=slot?4U:0U;s.next_pc=slot?0x40U:4U;s.delay_slot=slot;s.branch_pc=0;cpu.restore(s);
        const auto t=cpu.step(m);check(t.exception==0U && !t.retired && !t.instruction && cpu.state().pc==(bev?0xbfc00400U:0x80000200U) &&
            cpu.state().cop0.epc==0 && ((cpu.state().cop0.cause&0x80000000U)!=0)==slot,"timer vector and delay-slot EPC/BD");
    }
    // EI makes a previously pending interrupt eligible at the next boundary.
    auto s=CpuState{};s.cop0.status=0x8001;s.cop0.cause=0x8000;m.write(0,4,0x42000038);cpu.restore(s);
    check(cpu.step(m).retired && cpu.step(m).exception==0U && cpu.state().cop0.epc==4,"pending timer after EI");
    // Compare acknowledges only the internal source; INTC remains pending.
    m.write(0x10000020,4,1);m.write(0x10000010,4,0x180);m.write(0x1000f010,4,0x200);m.advance(1);
    s=CpuState{};s.cop0.cause=0x8000;s.cop0.status=2;s.gpr[1].low=99;m.write(0,4,move(4,1,11));cpu.restore(s);
    check(cpu.step(m).retired && (cpu.state().cop0.cause&0x8c00U)==0x400U,"Compare acknowledgement preserves external line");
}
void cache_hit_status() {
    Memory m;Cpu cpu;
    for(unsigned operation:{0x18U,0x1aU,0x1cU,0xbU,0xeU}) for(bool hit:{false,true}) for(bool old:{false,true}) {
        auto s=CpuState{};s.architectural_memory=true;s.pc=0xa0001000;s.next_pc=s.pc+4;s.cache.config=0x443;
        s.cop0.status=old?0x40000U:0;s.gpr[1].low=0x80000200;
        if(hit) cache_read(s.cache,m,0x80000200,0x200,4,operation<0x10);
        m.write(0x1000,4,0xbc200000U|(operation<<16U));cpu.restore(s);
        check(cpu.step(m).retired && ((cpu.state().cop0.status&0x40000U)!=0)==((operation==0x18 || operation==0x1a)?hit:old),"CH updates only DHIN/DHWBIN even with DCE clear");
    }
}
void guest_and_clocks() {
    System system;auto& m=system.memory();auto s=CpuState{};s.architectural_memory=true;s.pc=0xa0001000;s.next_pc=s.pc+4;system.cpu().restore(s);
    // Guest programs Compare=10, unmasks IM7/IE, then EI. Handler acknowledges,
    // counts one service and returns. No host callback dispatches the interrupt.
    const std::array<std::uint32_t,8> code{0x2401000a,move(4,1,11),0x34028001,move(4,2,12),0x42000038,0x24420001,0x1000ffff,0};
    const std::array<std::uint32_t,5> handler{move(0,3,13),move(4,0,11),0x24840001,0x42000018,0};
    for(unsigned n=0;n<code.size();++n)m.write(0x1000+4*n,4,code[n]);
    for(unsigned n=0;n<handler.size();++n)m.write(0x200+4*n,4,handler[n]);
    check(system.run(10).retired==10 && system.cpu().state().cop0.count==10 && (system.cpu().state().cop0.cause&0x8000U)!=0,"guest reaches timer match");
    const auto pending=system.state();std::vector<InstructionTrace>a,b;check(system.run(1,&a).retired==0 && a[0].exception==0U,"guest timer entry");
    const auto in_handler=system.state();check(system.run(8,&a).retired==8,"guest handler and return");const auto end=system.state();
    check(end.cpu.gpr[4].low==1 && (end.cpu.gpr[3].low&0x8000U)!=0 && (end.cpu.cop0.cause&0x8000U)==0 &&
          end.cpu.cop0.count==19 && (end.cpu.cop0.status&2U)==0,"guest acknowledgement/one service/continued clock");
    system.restore(pending);system.run(3,&b);system.run(6,&b);check(system.state()==end && a==b,"pending timer partitioned System replay");
    system.restore(in_handler);system.run(8);check(system.state()==end,"in-handler timer checkpoint replay");
    const auto before=system.state();system.run(0);check(system.state()==before,"zero budget preserves timer");
    // Direct run supplies diagnostic cycles; step leaves cycle assignment to caller.
    Cpu cpu;m.write(0,4,0);m.write(4,4,12);cpu.step(m);check(cpu.state().cop0.count==0,"step does not invent elapsed cycles");
    cpu.run(m,1);check(cpu.state().cop0.count==1,"exception boundary advances clock through run");
    cpu.reset();m.write(0,4,move(0,1,7));cpu.run(m,1);check(cpu.state().cop0.count==0,"host stop does not advance clock");
    // A real stalled FIFO boundary advances time even without retirement.
    System stalled;stalled.memory().write(0x10003000,4,8);
    for(unsigned n=0;n<16;++n)stalled.memory().write_quadword(0x10006000,{0x1000000000008000ULL,14});
    s=CpuState{};s.gpr[1].low=0x10006000;s.cop0.compare=1;stalled.cpu().restore(s);stalled.memory().write(0,4,0x7c220000);
    check(stalled.run(1).retired==0 && stalled.cpu().state().cop0.count==1 && (stalled.cpu().state().cop0.cause&0x8000U)!=0,"FIFO stall still reaches timer match");
}
}
int main(){try{clock_matches();transfers();enable_disable();interrupt_gates();cache_hit_status();guest_and_clocks();std::cout<<"COP0 timer and interrupt control tests passed\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
