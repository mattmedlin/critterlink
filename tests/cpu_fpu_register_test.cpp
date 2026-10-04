#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using namespace critterlink;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
constexpr std::uint32_t cop(unsigned mode,unsigned rt,unsigned fs){return (17U<<26U)|(mode<<21U)|(rt<<16U)|(fs<<11U);}
CpuState initial(){CpuState s;s.cop0.status=0x20000000;s.gpr[1]={0xabcdef0180000001ULL,0x12345678};s.fpu.accumulator=0xdeadbeef;return s;}
void run(Memory& m,Cpu& c,std::uint32_t op,CpuState before,CpuState expected){m.write(0,4,op);c.restore(before);expected.pc=4;expected.next_pc=8;auto t=c.step(m);check(t.retired&&!t.stop&&!t.exception&&c.state()==expected,"FPU register result/preserved state");}
void transfers(){
 Memory m;Cpu c;
 for(unsigned fs=0;fs<32;++fs)for(auto bits:{0U,1U,0x80000000U,0xffffffffU,0x7fc12345U,0x00000001U})for(unsigned rt:{0U,1U,31U}){
  auto s=initial();s.fpu.fpr[fs]=bits;auto e=s;if(rt)e.gpr[rt].low=bits<0x80000000U?bits:0xffffffff00000000ULL|bits;run(m,c,cop(0,rt,fs),s,e);
  s.gpr[31]={0x7654321000000000ULL|bits,~0ULL};e=s;e.fpu.fpr[fs]=static_cast<std::uint32_t>(s.gpr[rt].low);run(m,c,cop(4,rt,fs),s,e);
 }
}
void control(){
 Memory m;Cpu c;
 for(unsigned bit=0;bit<32;++bit){auto s=initial();s.gpr[1].low=1ULL<<bit;auto e=s;
  const bool writable=bit==23||bit==17||bit==16||bit==15||bit==14||bit==6||bit==5||bit==4||bit==3;
  e.fpu.control=0x01000001U|(writable?(1U<<bit):0U);run(m,c,cop(6,1,31),s,e);
 }
 for(auto value:{0U,0xffffffffU,0x78U,0x1c000U,0x20000U,0x800000U}){
  auto s=initial();s.gpr[1].low=value;auto e=s;e.fpu.control=0x01000001U|(value&0x0083c078U);run(m,c,cop(6,1,31),s,e);
  s=e;e=s;e.gpr[1].low=s.fpu.control;run(m,c,cop(2,1,31),s,e);
 }
 auto s=initial();auto e=s;e.gpr[1].low=0x2e30;run(m,c,cop(2,1,0),s,e);run(m,c,cop(6,1,0),s,s);
 run(m,c,cop(2,0,31),s,s);e=s;e.fpu.control=0x01000001;run(m,c,cop(6,0,31),s,e);
 // Guest COP0 writes enable/disable CU1; other supported bits survive.
 s=initial();s.gpr[1].low=0x20410c07;e=s;e.cop0.status=0x20410c07;run(m,c,(16U<<26U)|(4U<<21U)|(1U<<16U)|(12U<<11U),s,e);
 s=e;s.gpr[1].low=0;e=s;e.cop0.status=0;run(m,c,(16U<<26U)|(4U<<21U)|(1U<<16U)|(12U<<11U),s,e);
}
void memory(){
 Memory m;Cpu c;
 for(unsigned f:{0U,1U,31U})for(auto bits:{0U,0x80000000U,0x7fc12345U,0xffffffffU}){
  auto s=initial();s.gpr[1].low=0x2004;m.write(0x2000,4,bits);auto e=s;e.fpu.fpr[f]=bits;run(m,c,(49U<<26U)|(1U<<21U)|(f<<16U)|0xfffcU,s,e);
  s=e;m.write(0x2000,4,0);run(m,c,(57U<<26U)|(1U<<21U)|(f<<16U)|0xfffcU,s,s);check(m.read(0x2000,4)==bits,"SWC1 raw bits");
 }
 for(unsigned major:{49U,57U}){
  auto s=initial();s.gpr[1].low=0x2001;m.write(0,4,(major<<26U)|(1U<<21U));m.write(0x2000,4,0xabcdef01);c.restore(s);auto t=c.step(m);
  check(t.exception==(major==49?4U:5U)&&!t.retired&&!t.stop&&c.state().cop0.bad_vaddr==0x2001&&c.state().fpu==s.fpu&&m.read(0x2000,4)==0xabcdef01,"FPU alignment atomicity");
 }
}
void disabled(){
 Memory m;Cpu c;
 for(auto instruction:{cop(0,1,0),cop(4,1,0),cop(2,1,31),cop(6,1,31),(49U<<26U)|(1U<<21U),(57U<<26U)|(1U<<21U)})
 for(bool bev:{false,true})for(bool nested:{false,true})for(bool slot:{false,true}){
  auto s=initial();s.cop0.status=(bev?0x400000U:0U)|(nested?2U:0U);s.cop0.cause=0x30000000U;s.cop0.epc=0x100;s.cop0.bad_vaddr=0xabc;
  s.pc=slot?4:0;s.next_pc=slot?16:4;s.delay_slot=slot;s.branch_pc=0;s.gpr[1].low=0x2001;m.write(s.pc,4,instruction);m.write(0x2000,4,0x12345678);c.restore(s);
  auto t=c.step(m);auto e=s;e.cop0.status|=2;e.cop0.cause=0x1000002cU|(!nested&&slot?0x80000000U:0U);if(!nested)e.cop0.epc=0;
  e.pc=bev?0xbfc00380U:0x80000180U;e.next_pc=e.pc+4;e.delay_slot=false;e.branch_pc=0;
  check(t.exception==11U&&!t.retired&&!t.stop&&c.state()==e&&m.read(0x2000,4)==0x12345678,"COP1 unusable CE/EPC/BD/priority");
 }
}
void invalid(){
 Memory m;Cpu c;std::vector<std::uint32_t> bad{cop(2,1,1),cop(6,1,16),0x46000008U};
 for(unsigned mode:{0U,2U,4U,6U})for(unsigned bit=0;bit<11;++bit)bad.push_back(cop(mode,1,mode==2||mode==6?31:0)|(1U<<bit));
 for(auto op:bad){auto s=initial();m.write(0,4,op);c.restore(s);auto t=c.step(m);s.stop=c.state().stop;check(t.stop&&!t.retired&&!t.exception&&c.state()==s,"unsupported FPU encoding changed state");}
 auto s=initial();c.restore(s);for(auto value:{0U,0x01000003U,0xffffffffU}){auto badstate=s;badstate.fpu.control=value;bool rejected=false;try{c.restore(badstate);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&c.state()==s,"invalid FCR snapshot atomicity");}
 c.reset();check(c.state().fpu==FpuState{}&&c.state().cop0.status==0,"synthetic FPU reset");
}
void delay_slots(){
 Memory m;Cpu c;
 for(auto instruction:{cop(0,1,0),cop(4,1,0),cop(2,1,31),cop(6,1,31),(49U<<26U)|(1U<<21U),(57U<<26U)|(1U<<21U)})
 for(auto branch:{0x10000003U,0x14000003U,0x54000003U}){
  auto s=initial();s.gpr[1].low=0x2000;s.fpu.fpr[0]=0x80000001;m.write(0x2000,4,0x7fc12345);m.write(0,4,branch);m.write(4,4,instruction);c.restore(s);
  check(c.step(m).retired,"FPU branch setup");
  if(branch==0x54000003U)check(c.state().pc==8&&c.state().fpu==s.fpu&&c.state().gpr==s.gpr&&m.read(0x2000,4)==0x7fc12345,"annulled FPU instruction executed");
  else {const auto saved=c.state();const auto first=c.step(m);const auto expected=c.state();const auto value=m.read(0x2000,4);m.write(0x2000,4,0x7fc12345);c.restore(saved);
   check(c.step(m)==first&&c.state()==expected&&m.read(0x2000,4)==value&&first.retired&&first.delay_slot&&expected.pc==(branch==0x10000003U?16U:8U),"FPU delay replay");}
 }
}
void guest(){
 System system;auto& m=system.memory();
 // First access faults. Handler enables CU1 and retries through ERET.
 m.write(0,4,cop(4,1,0));m.write(4,4,cop(0,2,0));m.write(8,4,(57U<<26U)|(4U<<21U));m.write(12,4,cop(6,3,31));m.write(16,4,cop(2,5,31));
 m.write(0x180,4,0x3c062000);m.write(0x184,4,(16U<<26U)|(4U<<21U)|(6U<<16U)|(12U<<11U));m.write(0x188,4,0x42000018);
 CpuState s;s.gpr[1].low=0x80000001;s.gpr[3].low=~0ULL;s.gpr[4].low=0x2000;system.cpu().restore(s);
 check(system.run(1)==RunResult{0,true}&&system.cpu().state().cop0.epc==0,"FPU guest first trap");auto saved=system.state();std::vector<InstructionTrace>a,b;
 check(system.run(8,&a)==RunResult{8,true}&&m.read(0x2000,4)==0x80000001&&system.cpu().state().gpr[2].low==0xffffffff80000001ULL&&system.cpu().state().gpr[5].low==0x0183c079,"FPU guest recovery and RAM");
 auto expected=system.state();system.restore(saved);system.run(3,&b);system.run(5,&b);check(system.state()==expected&&a==b,"FPU full-System replay");
}
}
int main(){try{transfers();control();memory();disabled();invalid();delay_slots();guest();std::cout<<"FPU register tests passed\n";}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
