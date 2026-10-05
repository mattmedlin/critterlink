#include "critterlink/system.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace critterlink;
constexpr std::array<std::uint32_t, 6> bases{0x1f801100,0x1f801110,0x1f801120,0x1f801480,0x1f801490,0x1f8014a0};
constexpr std::array<std::uint32_t, 6> irqs{0x10,0x20,0x40,0x4000,0x8000,0x10000};
void check(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid IOP timer operation accepted");
}
void registers_and_events() {
    for (unsigned i=0;i<6;++i) {
        IopTimers timers; const auto base=bases[i], maximum=i<3?0xffffU:0xffffffffU;
        check(timers.read(base)==0 && timers.read(base+4,2)==0x400 && timers.read(base+8)==0,"timer initial policy");
        timers.write(base,0xffffffff);timers.write(base+8,0xffffffff);
        check(timers.read(base)==maximum && timers.read(base+8)==maximum,"timer count/target width");
        timers.write(base+4,0xffffffff,2);
        check(timers.read(base)==0 && timers.read(base+4)==0x67ff,"MODE masks readonly bits and resets count");
        timers.write(base+4,0); timers.write(base+8,1); timers.write(base,maximum-1);
        check(timers.advance_sysclock(1)==0 && timers.read(base)==maximum && timers.read(base+4)==0x400,"pre-wrap count");
        check(timers.advance_sysclock(1)==0 && timers.read(base)==0,"overflow wraps at width");
        check(timers.advance_sysclock(1)==0 && timers.read(base)==1 && timers.read(base+4)==0x1c00 &&
              timers.read(base+4)==0x400,"unmasked-independent flags clear on read");
        timers.write(base+4,0x18);timers.write(base+8,3);
        check(timers.advance_sysclock(10)==irqs[i] && timers.read(base)==1 && timers.read(base+4)==0x818,"one-shot target reset/disable");
        check(timers.advance_sysclock(5)==0 && timers.read(base)==0,"one-shot suppression");
        timers.write(base+8,4);check(timers.advance_sysclock(4)==irqs[i],"target write rearms pulse one-shot");
        timers.write(base+4,0xd8);timers.write(base+8,2);
        for(unsigned event=0;event<4;++event) {
            check(timers.advance_sysclock(2)==(event%2==0?irqs[i]:0U),"repeat toggle event sequence");
            check((timers.read(base+4)&0x400)==(event%2==0?0U:0x400U),"repeat toggle status parity");
        }
        timers.write(base+4,0x58);timers.write(base+8,2);
        for(unsigned event=0;event<3;++event) {
            check(timers.advance_sysclock(1)==0 && timers.advance_sysclock(1)==irqs[i] &&
                  (timers.read(base+4)&0x400)!=0,"repeat pulse stays armed");
        }
        timers.write(base+4,0x98);timers.write(base+8,1);
        check(timers.advance_sysclock(1)==irqs[i],"one-shot level first request");
        timers.write(base+8,1);check(timers.advance_sysclock(2)==0,"LEVL target write must not rearm");
        timers.write(base+4,0xf0);timers.write(base+8,0);timers.write(base,maximum);
        check(timers.advance_sysclock(1)==irqs[i] && (timers.read(base+4)&0x1c00)==0x1800,
              "simultaneous target-zero/wrap event toggles once");
        check(timers.advance_sysclock(std::uint64_t{maximum}+1)==0 && (timers.read(base+4)&0x400)!=0,
              "second coincident event releases toggle");
        timers.write(base+4,8);timers.write(base+8,2);
        check(timers.advance_sysclock(7)==0 && timers.read(base)==1 && (timers.read(base+4)&0x800)!=0,
              "reset-on-target independent of compare IRQ enable");
        const auto saved=timers.state();
        rejects([&]{(void)timers.read(base+4,1);});rejects([&]{timers.write(base+2,0,2);});
        rejects([&]{(void)timers.read(base+12);});rejects([&]{timers.write(base+4,0,8);});
        if(i>=3) {rejects([&]{timers.write(base+8,0,2);});rejects([&]{(void)timers.read(base,2);});}
        auto bad=saved;bad.timers[i].mode|=0x8000;rejects([&]{timers.restore(bad);});
        bad=saved;bad.timers[i].phase=256;rejects([&]{timers.restore(bad);});
        bad=saved;bad.timers[i].gate_wait=true;rejects([&]{timers.restore(bad);});
        if(i<3) {bad=saved;bad.timers[i].count=0x10000;rejects([&]{timers.restore(bad);});}
        check(timers.state()==saved,"rejected timer operation mutated state");
    }
}
void clocks_and_gates() {
    IopTimers timers;
    for(unsigned i:{0U,1U,3U}) {timers.write(bases[i]+4,0x100);}
    timers.advance_sysclock(17);check(timers.read(bases[0])==0 && timers.read(bases[1])==0 && timers.read(bases[3])==0,"external clocks advanced on sysclock");
    timers.advance_pixel(7);check(timers.read(bases[0])==7,"pixel ticks");
    timers.set_hblank(true);timers.set_hblank(true);check(timers.read(bases[1])==1 && timers.read(bases[3])==1,"HBlank clocks only on entry");
    timers.set_hblank(false);timers.set_hblank(true);check(timers.read(bases[1])==2 && timers.read(bases[3])==2,"second HBlank clock");
    for(unsigned i:{0U,1U,3U}) for(unsigned gate=0;gate<4;++gate) {
        IopTimers gated;const auto base=bases[i];gated.write(base+4,1|(gate<<1));gated.advance_sysclock(3);
        check(gated.read(base)==(gate<2?3U:0U),"gate before blank");
        const auto blank=[&](bool value){if(i==0){gated.set_hblank(value);}else{gated.set_vblank(value);}};
        blank(true);gated.advance_sysclock(2);
        check(gated.read(base)==(gate==0?3U:2U),"gate inside blank");
        blank(false);gated.advance_sysclock(4);
        check(gated.read(base)==(gate==0?7U:gate==2?2U:6U),"gate after blank");
        if(gate==3) {
            blank(true);gated.write(base+4,7);gated.advance_sysclock(3);
            check(gated.read(base)==0 && gated.state().timers[i].gate_wait,"wait for next blank when configured high");
            const auto saved=gated.state();blank(false);blank(true);gated.advance_sysclock(2);const auto expected=gated.state();
            gated.restore(saved);blank(false);blank(true);gated.advance_sysclock(2);check(gated.state()==expected,"gate-wait replay");
        }
    }
    for(unsigned i:{2U,4U,5U}) for(unsigned gate=0;gate<4;++gate) {
        IopTimers gated;gated.write(bases[i]+4,0x101|(gate<<1));gated.advance_sysclock(5);
        check(gated.read(bases[i])==((gate==1||gate==2)?5U:0U),"ungated-source timer gate policy / bit8 ignored");
    }
    for(unsigned i:{2U,4U,5U}) for(unsigned divide:{1U,8U,16U,256U}) {
        if(i==2 && divide>8) {continue;}
        IopTimers divided;const unsigned selector=divide==1?0:divide==8?1:divide==16?2:3;
        divided.write(bases[i]+4,i==2?(selector<<9):(selector<<13));
        divided.advance_sysclock(divide-1);check(divided.read(bases[i])==0,"divider early tick");
        const auto saved=divided.state();divided.advance_sysclock(1);const auto expected=divided.state();
        check(divided.read(bases[i])==1 && divided.state().timers[i].phase==0,"divider boundary");
        divided.restore(saved);divided.advance_sysclock(1);check(divided.state()==expected,"divider phase replay");
    }
}
void reset_boundaries() {
    for(unsigned i:{0U,3U}) {
        IopTimers timers;const auto base=bases[i],maximum=i==0?0xffffU:0xffffffffU;
        timers.write(base+4,0x58);timers.write(base+8,3);timers.write(base,maximum-1);
        check(timers.advance_sysclock(6)==irqs[i] && timers.read(base)==1 && timers.read(base+4)==0x1c58,
              "reset target below initial count wraps before compare");
        timers.write(base+4,0x58);timers.write(base+8,2);timers.write(base,2);
        const auto saved=timers.state();
        check(timers.advance_sysclock(1)==0 && timers.read(base)==3,"count equality from write must not trigger compare");
        timers.restore(saved);
        check(timers.advance_sysclock(std::uint64_t{maximum}+4)==irqs[i] && timers.read(base)==1 && timers.read(base+4)==0x1c58,
              "equal-count target waits full wrap then resumes target period");
    }
    IopTimers divided;divided.write(bases[2]+4,0x200);divided.advance_sysclock(7);divided.write(bases[2],123);
    divided.advance_sysclock(1);check(divided.read(bases[2])==124,"COUNT write preserves divider phase policy");
    divided.write(bases[2]+4,0x200);divided.advance_sysclock(1);divided.write(bases[2]+8,7);
    divided.advance_sysclock(7);check(divided.read(bases[2])==1,"MODE resets phase / TARGET preserves it");
}
void large_advances() {
    constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
    for(unsigned i=0;i<6;++i) for(unsigned mode:{0x50U,0xf0U,0x58U,0xf8U,0x6250U}) for(unsigned target:{0U,1U,7U,0xffffU}) {
        IopTimers whole;whole.write(bases[i]+4,mode);whole.write(bases[i]+8,target);whole.write(bases[i],0xfffe);
        IopTimers split;split.restore(whole.state());const auto a=whole.advance_sysclock(maximum);
        auto b=split.advance_sysclock(maximum-12345);b|=split.advance_sysclock(12345);
        check(whole.state()==split.state() && a==b,"large timer advance differs from partitioned elapsed clocks");
        const auto saved=whole.state();check(whole.advance_sysclock(0)==0 && whole.advance_pixel(0)==0 && whole.state()==saved,"zero clocks mutate timer state");
    }
    IopTimers pulse;pulse.write(bases[5]+4,0x58);pulse.write(bases[5]+8,1);
    check(pulse.advance_sysclock(maximum)==irqs[5] && pulse.read(bases[5])==0 && pulse.read(bases[5]+4)==0xc58,"UINT64_MAX target-one pulses");
    IopTimers toggle;toggle.write(bases[5]+4,0xf8);toggle.write(bases[5]+8,1);toggle.write(bases[5],0xffffffff);
    check(toggle.advance_sysclock(maximum)==irqs[5] && toggle.read(bases[5])==0 && toggle.read(bases[5]+4)==0x18f8,"UINT64_MAX event union/parity");
    IopTimers divided;divided.write(bases[2]+4,0x250);divided.advance_sysclock(7);
    check(divided.advance_sysclock(maximum)==irqs[2] && divided.read(bases[2])==0 && divided.state().timers[2].phase==6,"UINT64_MAX divider carry");
    IopIntc intc;intc.raise_edges(0x1c070);check(intc.read(0x1f801070)==0x1c070 && intc.state().levels==0,"timer event latches independent of level history");
    const auto saved=intc.state();rejects([&]{intc.raise_edges(0x80000000);});check(intc.state()==saved,"invalid event mutation");
}
constexpr std::uint32_t imm(unsigned op,unsigned rs,unsigned rt,unsigned value) {
    return (op<<26U)|(rs<<21U)|(rt<<16U)|value;
}
void guest_handlers() {
    for(unsigned i=0;i<6;++i) {
        System system;auto& iop=system.memory().iop();iop.start(0x300);
        auto initial=iop.state();initial.architectural_exceptions=true;iop.restore(initial);
        system.memory().write(0,4,0x1000ffff);system.memory().write(4,4,0);
        std::uint32_t pc=0x300;
        const auto emit=[&](std::uint32_t value){iop.write32(pc,value);pc+=4;};
        const auto li=[&](unsigned reg,std::uint32_t value){emit(imm(15,0,reg,value>>16U));emit(imm(13,reg,reg,value&0xffffU));};
        li(1,0xbf801070);li(2,irqs[i]);emit(imm(43,1,2,4));
        emit(imm(9,0,2,1));emit(imm(43,1,2,8));
        li(1,bases[i]|0xa0000000);emit(imm(9,0,2,100));emit(imm(i<3?41:43,1,2,8));
        emit(imm(9,0,2,0x18));emit(imm(41,1,2,4));
        emit(imm(9,0,3,0x401));emit(0x40836000);emit(0x1000ffff);emit(0);
        pc=0x80;emit(0x401a7000); // MFC0 k0,EPC, delayed
        li(1,bases[i]|0xa0000000);emit(imm(37,1,4,4));emit(0);emit(imm(43,0,4,0x1000));
        emit(imm(37,1,5,4));emit(0);emit(imm(43,0,5,0x1004));
        li(2,~irqs[i]);li(3,0xbf801070);emit(imm(43,3,2,0));
        emit(imm(9,20,20,1));emit(imm(43,0,20,0x1008));
        emit(imm(9,0,6,2));emit(imm(4,20,6,3));emit(0); // Rearm only after the first service.
        emit(imm(9,0,2,100));emit(imm(i<3?41:43,1,2,8));
        emit(0x03400008);emit(0x42000010);
        for(unsigned n=0;n<200 && system.memory().hardware().iop_timers().timers[i].count!=99;++n) {
            check(system.run(1).budget_exhausted,"timer guest setup stopped");
        }
        const auto before=system.state();
        check(before.memory.hardware.iop_timers.timers[i].count==99 && before.memory.hardware.iop_intc.status==0,"timer pre-compare checkpoint");
        check(system.run(1).budget_exhausted && iop.state().pc==0x80000080 &&
              system.memory().hardware().state().iop_intc.status==irqs[i],"timer event must enter guest interrupt handler");
        check(system.run(1).budget_exhausted,"timer handler first instruction");const auto handler=system.state();
        check(handler.memory.hardware.iop.pending_load && handler.memory.hardware.iop.pending_load->reg==26,"timer handler pending EPC read");
        std::vector<InstructionTrace> first,second;check(system.run(170,&first).budget_exhausted,"timer handler continuation");
        check(iop.read32(0x1000)==0x818 && iop.read32(0x1004)==0x18 && iop.read32(0x1008)==2 &&
              system.memory().hardware().state().iop_intc.status==0 && (iop.state().cop0.status&0x401)==0x401,
              "timer guest flags, acknowledgement, service count or return");
        const auto end=system.state();system.restore(handler);
        check(system.run(17,&second).budget_exhausted && system.run(153,&second).budget_exhausted &&
              system.state()==end && first==second,"timer handler replay differs");
        system.restore(before);check(system.run(172).budget_exhausted && system.state()==end,"pre-compare timer replay differs");
        auto bad=end;bad.memory.hardware.iop_timers.timers[i].phase=256;
        rejects([&]{system.restore(bad);});check(system.state()==end,"invalid system timer restore mutated state");
    }
}
void external_hardware_clock() {
    Hardware hardware;auto saved=hardware.state();saved.iop_timers.timers[0].mode=0x558;
    saved.iop_timers.timers[0].target=2;saved.iop_intc.mask=0x10;saved.iop_intc.enabled=true;
    hardware.restore(saved);hardware.advance_iop_pixel_clock(1);const auto before=hardware.state();
    check(before.iop_timers.timers[0].count==1 && before.iop_intc.status==0,"external pixel before compare");
    hardware.advance_iop_pixel_clock(1);const auto after=hardware.state();
    check(after.iop_intc.status==0x10 && (after.iop.cop0.cause&0x400)!=0 && after.scheduler==before.scheduler,
          "explicit pixel event must route IRQ independently of scheduler ticks");
    hardware.restore(before);hardware.advance_iop_pixel_clock(1);check(hardware.state()==after,"external pixel replay");
    hardware.advance_iop_pixel_clock(0);check(hardware.state()==after,"zero external clocks mutate hardware");
}
}
int main() {
    try {registers_and_events();clocks_and_gates();reset_boundaries();large_advances();guest_handlers();external_hardware_clock();
        std::cout<<"IOP timer functional-model tests passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
