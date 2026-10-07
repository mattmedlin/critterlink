#include "critterlink/system.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace critterlink;
constexpr std::uint32_t stat=0x1000f000, mask=0x1000f010;
constexpr std::uint32_t reg(unsigned n,unsigned offset=0){return 0x10000000+n*0x800+offset;}
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class T> auto snap(const T& object){using S=std::remove_cvref_t<decltype(object.state())>;return std::unique_ptr<S>(new S(object.state()));}
template<class T> auto copy(const T& value){return std::unique_ptr<T>(new T(value));}
template<class F> void invalid(F f){try{f();}catch(const std::invalid_argument&){return;}throw std::runtime_error("expected invalid state rejection");}
void level(Hardware& h,bool vertical,bool high){if(vertical)h.set_ee_vblank(high);else h.set_ee_hblank(high);}
void pulse(Hardware& h){h.set_ee_hblank(false);h.set_ee_hblank(true);}
unsigned divisor(unsigned clock){return clock==0?1:clock==1?16:256;}
std::uint32_t mode(unsigned clock,bool vertical,unsigned gate){return 0x84U|clock|(vertical?8U:0U)|(gate<<4);}

// Sony EE manual: gate0 counts low; gate1/2/3 reset/start on rising/falling/both.
// Initial waiting and divider preservation are explicitly diagnostic policies.
void gate_matrix(){
 for(unsigned n=0;n<4;++n)for(unsigned clock=0;clock<3;++clock)for(bool vertical:{false,true})for(unsigned gate=0;gate<4;++gate){
  auto hp=std::make_unique<Hardware>();auto& h=*hp;const auto d=divisor(clock);
  h.write(reg(n,0x10),mode(clock,vertical,gate));
  h.advance(d,{});check(h.read(reg(n))==(gate==0?1U:0U),"initial gate admission");
  level(h,vertical,true);h.advance(d,{});
  check(h.read(reg(n))==(gate==0?1U:gate==2?0U:1U),"rising gate behavior");
  const auto before=snap(h);level(h,vertical,true);check(*snap(h)==*before,"duplicate blank level mutated state");
  level(h,vertical,false);h.advance(d,{});
  check(h.read(reg(n))==(gate==0||gate==1?2U:1U),"falling gate behavior/free run");
  if(gate!=0){level(h,vertical,true);if(gate==2)level(h,vertical,false);check(h.read(reg(n))==0,"selected edge did not reset");}
 }
}
void external_matrix(){
 for(unsigned n=0;n<4;++n)for(unsigned gate=0;gate<4;++gate){
  auto hp=std::make_unique<Hardware>();auto& h=*hp;
  h.write(reg(n,0x10),mode(3,false,gate));h.advance(9,{});check(h.read(reg(n))==0,"external clock advanced on bus");
  pulse(h);check(h.read(reg(n))==1,"external HBlank must disable HBlank gate");
  h.set_ee_hblank(true);check(h.read(reg(n))==1,"duplicate HBlank counted");
  h.set_ee_hblank(false);check(h.read(reg(n))==1,"falling external clock counted under rising-edge policy");
  h.write(reg(n,0x10),mode(3,false,gate)&~0x80U);pulse(h);check(h.read(reg(n))==1,"CUE did not suppress external count");
 }
 for(unsigned n=0;n<4;++n)for(unsigned gate=0;gate<4;++gate){
  auto hp=std::make_unique<Hardware>();auto& h=*hp;h.write(reg(n,0x10),mode(3,true,gate));
  pulse(h);check(h.read(reg(n))==(gate==0?1U:0U),"external VBlank initial admission");
  h.set_ee_vblank(true);pulse(h);check(h.read(reg(n))==(gate==0?1U:gate==2?0U:1U),"external rising gate order");
  h.set_ee_vblank(false);pulse(h);check(h.read(reg(n))==(gate<2?2U:1U),"external falling gate order");
 }
}
void phase_and_write_policies(){
 for(unsigned clock:{1U,2U})for(bool vertical:{false,true}){
  auto hp=std::make_unique<Hardware>();auto& h=*hp;const auto d=divisor(clock);h.write(reg(0,0x10),mode(clock,vertical,0));
  h.advance(d-1,{});level(h,vertical,true);h.advance(d+3,{});level(h,vertical,false);h.advance(1,{});
  check(h.read(reg(0))==1,"gate pause discarded divider phase");
  h.write(reg(0,0x10),mode(clock,vertical,1));h.advance(d-1,{});level(h,vertical,true);
  h.advance(d-1,{});check(h.read(reg(0))==0,"edge reset did not restart divider");h.advance(1,{});check(h.read(reg(0))==1,"edge divider boundary");
  h.write(reg(0,0x10),mode(clock,vertical,1)|0xc00);h.advance(d,{});check(h.read(reg(0))==2,"flag acknowledgement rearmed gate");
  h.write(reg(0,0x10),mode(clock,vertical,1)&~0x80U);level(h,vertical,false);level(h,vertical,true);
  check(h.read(reg(0))==0&&!snap(h)->timers[0].gate_wait,"CUE-off edge policy");
  h.write(reg(0,0x10),mode(clock,vertical,1));h.advance(d,{});check(h.read(reg(0))==1,"CUE restart lost activated gate");
  h.write(reg(0,0x10),mode(clock,vertical,2));check(snap(h)->timers[0].gate_wait,"gate reconfiguration did not arm wait");
  level(h,vertical,false);h.advance(d-1,{});h.write(reg(0),7);h.advance(1,{});check(h.read(reg(0))==7,"COUNT write phase reset policy");
 }
}
void interrupts(){
 for(unsigned n=0;n<4;++n){
  auto hp=std::make_unique<Hardware>();auto& h=*hp;const auto irq=1U<<(9+n);
  h.write(reg(n,0x20),0);h.write(reg(n),0xffff);h.write(reg(n,0x10),0x383);pulse(h);
  check(h.read(reg(n))==0&&(h.read(reg(n,0x10))&0xc00)==0xc00&&h.read(stat)==irq&&!h.int0(),"external zero compare and overflow");
  h.write(mask,irq);check(h.int0(),"masked timer flag did not latch");h.write(stat,irq);check(!h.int0(),"INTC W1C");
  h.write(reg(n),0xffff);pulse(h);check(h.read(stat)==0,"uncleared timer flag retriggered");
  h.write(reg(n,0x10),0x783);check((h.read(reg(n,0x10))&0xc00)==0x800,"compare-only W1C cleared overflow");
  h.write(reg(n),0xffff);pulse(h);check(h.read(stat)==irq,"compare rearm failed");
  h.write(reg(n,0x10),0xfc3);h.write(reg(n,0x20),1);pulse(h);check(h.read(reg(n))==0,"ZRET with external clock");
  h.write(reg(n,0x10),0xcc3);h.write(stat,irq);pulse(h);check(h.read(reg(n))==0&&(h.read(reg(n,0x10))&0xc00)==0&&h.read(stat)==0,"ZRET requires no interrupt enable");
 }
 auto hp=std::make_unique<Hardware>();auto& h=*hp;h.set_ee_vblank(true);check(h.read(stat)==4&&!h.int0(),"VBlank start latch");h.write(stat,4);h.set_ee_vblank(true);check(h.read(stat)==0,"duplicate VBlank retrigger");h.set_ee_vblank(false);check(h.read(stat)==8,"VBlank end latch");
}
void snapshot_policies(){
 auto hp=std::make_unique<Hardware>();auto& h=*hp;h.write(reg(0,0x10),mode(1,true,1));h.set_ee_vblank(true);h.advance(7,{});
 check(snap(h)->ee_vblank&&!snap(h)->ee_hblank,"independent retained EE blank levels");
 const auto before=snap(h);h.advance(9,{});h.set_ee_vblank(false);const auto after=snap(h);
 h.restore(*before);h.advance(3,{});h.advance(6,{});h.set_ee_vblank(false);check(*snap(h)==*after,"partial-divider replay");
 for(unsigned kind=0;kind<4;++kind){auto bad=copy(*after);if(kind==0)bad->timers[0].phase=16;if(kind==1){bad->timers[1].mode=0x83;bad->timers[1].phase=1;}if(kind==2)bad->timers[1].gate_wait=true;if(kind==3)bad->timers[1].mode=0x1000;invalid([&]{h.restore(*bad);});check(*snap(h)==*after,"invalid restore changed hardware");}
 // Provisional phase/configuration rules; these are not new silicon observations.
 h.set_ee_hblank(true);h.write(reg(2,0x10),mode(1,false,1));h.advance(16,{});
 check(h.read(reg(2))==0&&snap(h)->timers[2].gate_wait,"configuration while gate high must wait for new edge");
 h.set_ee_hblank(false);h.set_ee_hblank(true);h.advance(7,{});
 h.write(reg(2,0x10),mode(1,false,1)|0x340);h.advance(9,{});
 check(h.read(reg(2))==1,"IRQ/ZRET-only mode write discarded divider phase");
 h.advance(7,{});h.write(reg(2,0x10),mode(1,false,1)&~0x80U);
 check(snap(h)->timers[2].phase==0&&!snap(h)->timers[2].gate_wait,"CUE change phase/wait policy");
 h.write(reg(2,0x10),mode(2,false,1));check(snap(h)->timers[2].gate_wait&&snap(h)->timers[2].phase==0,"clock change did not reset/rearm");
}
void original_guest_replay(){
 auto sp=std::make_unique<System>();auto& s=*sp;auto& m=s.memory();
 // Original EE setup writes external-clock/VBlank rising gate+compare IRQ, then loops.
 constexpr std::uint32_t guest[]{0x3c081000,0x34090001,0xad090020,0x3409019f,0xad090010,0x1000ffff,0};
 for(unsigned i=0;i<sizeof(guest)/sizeof(*guest);++i)m.write(i*4,4,guest[i]);
 // Handler reads count/mode, saves observations, acknowledges timer and video INTC,
 // clears compare flag while preserving configuration, then returns via ERET.
 constexpr std::uint32_t handler[]{0x3c081000,0x8d090000,0x8d0a0010,0xac091000,0xac0a1004,0x340b059f,0xad0b0010,0x3c0c1001,0x340b020c,0xad8bf000,0x26100001,0x42000018};
 for(unsigned i=0;i<sizeof(handler)/sizeof(*handler);++i)m.write(0x200+i*4,4,handler[i]);
 auto cpu=snap(s.cpu());cpu->cop0.status=0x10401;s.cpu().restore(*cpu);m.write(mask,4,0x200);
 check(s.run(5).budget_exhausted,"guest setup");const auto waiting=snap(s);std::vector<InstructionTrace>a,b;
 m.set_ee_vblank(true);m.set_ee_hblank(true);check(s.run(16,&a).budget_exhausted,"guest IRQ run");
 check(s.cpu().state().gpr[16].low==1&&m.read(0x1000,4)==1&&(m.read(0x1004,4)&0x400)!=0&&!m.hardware().int0(),"guest timer handler outcome");
 const auto expected=snap(s);s.restore(*waiting);m.set_ee_vblank(true);m.set_ee_hblank(true);check(s.run(6,&b).budget_exhausted&&s.run(10,&b).budget_exhausted&&*snap(s)==*expected&&a==b,"full System gate/IRQ replay");
 auto bad=copy(*expected);bad->memory.hardware.timers[0].phase=1;invalid([&]{s.restore(*bad);});check(*snap(s)==*expected,"System invalid external-phase restore not atomic");
}
}
int main(){try{gate_matrix();external_matrix();phase_and_write_policies();interrupts();snapshot_policies();original_guest_replay();std::cout<<"EE timer gates functional tests passed\n";}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
