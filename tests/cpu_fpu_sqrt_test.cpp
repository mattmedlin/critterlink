#include "critterlink/system.hpp"
#include "cpu_fpu_sqrt_vectors.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using namespace critterlink;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
constexpr std::uint32_t op(unsigned ft,unsigned fd){return 0x46000004U|(ft<<16U)|(fd<<6U);}
CpuState initial(){CpuState s;s.cop0.status=0x20000000;s.fpu.control=0x0183c039;s.fpu.accumulator=0xdeadbeef;return s;}
void result(Memory& m,Cpu& cpu,std::uint32_t input,std::uint32_t output,unsigned ft=0,unsigned fd=31){
 auto s=initial();s.fpu.fpr[ft]=input;auto expected=s;expected.pc=4;expected.next_pc=8;expected.fpu.fpr[fd]=output;
 expected.fpu.control=(input&0x80000000U)!=0?0x0182c079U:0x0180c039U;
 m.write(0,4,op(ft,fd));cpu.restore(s);auto t=cpu.step(m);
 if(!(t.retired&&!t.stop&&!t.exception&&cpu.state()==expected)) {
  std::cerr<<std::hex<<"input="<<input<<" expected="<<output<<" actual="<<cpu.state().fpu.fpr[fd]
           <<" expected flags="<<expected.fpu.control<<" actual flags="<<cpu.state().fpu.control
           <<std::dec<<" ft="<<ft<<" fd="<<fd<<'\n';
 }
 check(t.retired&&!t.stop&&!t.exception&&cpu.state()==expected,"SQRT result, flags or preserved state");
}
void vectors(){
 Memory m;Cpu cpu;
 for(const auto& v:fpu_sqrt_vectors)for(unsigned fd:{0U,1U,31U})result(m,cpu,v[0],v[1],0,fd);
 for(auto bits:{0U,1U,0x007fffffU}){result(m,cpu,bits,0);result(m,cpu,bits|0x80000000U,0);}
 for(unsigned sign:{0U,0x80000000U}){
  result(m,cpu,0x3fc00000U|sign,0x3f9cc471);
  result(m,cpu,0x3fffffffU|sign,0x3fb504f3);
  result(m,cpu,0x7fc00000U|sign,0x5f9cc471);
  result(m,cpu,0x00800000U|sign,0x20000000);
 }
 // Powers of four have exact roots; twice those powers scales the known sqrt(2).
 for(int power=-63;power<=64;++power){
  const auto input=static_cast<std::uint32_t>(2*power+127)<<23U;
  const auto output=static_cast<std::uint32_t>(power+127)<<23U;
  result(m,cpu,input,output);result(m,cpu,input|0x80000000U,output);
 }
 for(int power=-63;power<64;++power){
  const auto input=static_cast<std::uint32_t>(2*power+128)<<23U;
  const auto output=(static_cast<std::uint32_t>(power+127)<<23U)|0x003504f3U;
  result(m,cpu,input,output);result(m,cpu,input|0x80000000U,output);
 }
 for(unsigned ft=0;ft<32;++ft)for(unsigned fd=0;fd<32;++fd){
  result(m,cpu,0x3fc00000,0x3f9cc471,ft,fd);result(m,cpu,0xbfc00000,0x3f9cc471,ft,fd);
 }
 // SI stays set after a later positive square root; current I/D clear.
 auto s=initial();s.fpu.control=0x0183c079;s.fpu.fpr[0]=0x40800000;auto e=s;
 e.pc=4;e.next_pc=8;e.fpu.fpr[1]=0x40000000;e.fpu.control=0x0180c079;
 m.write(0,4,op(0,1));cpu.restore(s);check(cpu.step(m).retired&&cpu.state()==e,"SQRT sticky flags");
}
void flow(){
 Memory m;Cpu cpu;
 for(bool slot:{false,true}){
  auto s=initial();s.cop0.status=0;s.pc=slot?4U:0U;s.next_pc=8;s.delay_slot=slot;
  m.write(s.pc,4,op(0,31));cpu.restore(s);auto t=cpu.step(m);
  check(t.exception==11U&&!t.retired&&!t.stop&&cpu.state().fpu==s.fpu&&
        cpu.state().cop0.cause==(slot?0x9000002cU:0x1000002cU),"SQRT CU1");
 }
 for(auto branch:{0x10000003U,0x14000003U,0x54000003U}){
  auto s=initial();s.fpu.fpr[0]=0xbf800000;m.write(0,4,branch);m.write(4,4,op(0,0));cpu.restore(s);
  check(cpu.step(m).retired,"SQRT branch setup");
  if(branch==0x54000003U)check(cpu.state().pc==8&&cpu.state().fpu==s.fpu,"SQRT annul");
  else{const auto saved=cpu.state();auto t=cpu.step(m);const auto e=cpu.state();cpu.restore(saved);
   check(t.retired&&t.delay_slot&&cpu.step(m)==t&&cpu.state()==e,"SQRT slot replay");}
 }
 for(unsigned reserved=1;reserved<32;++reserved){
  auto s=initial();m.write(0,4,op(0,31)|(reserved<<11U));cpu.restore(s);auto t=cpu.step(m);s.stop=cpu.state().stop;
  check(t.stop&&!t.retired&&!t.exception&&cpu.state()==s,"SQRT reserved fs field");
 }
}
void guest(){
 System system;auto& m=system.memory();
 const std::array<std::uint32_t,7> program{op(0,1),op(2,3),0x4444f800,0xe4010400,0xe4030404,0xac040408,0};
 for(std::size_t i=0;i<program.size();++i)m.write(static_cast<std::uint32_t>(i*4),4,program[i]);
 auto s=initial();s.fpu.fpr[0]=0x3fc00000;s.fpu.fpr[2]=0x80000000;system.cpu().restore(s);
 check(system.run(1)==RunResult{1,true},"SQRT guest prefix");const auto saved=system.state();std::vector<InstructionTrace>a,b;
 check(system.run(5,&a)==RunResult{5,true}&&m.read(0x400,4)==0x3f9cc471U&&m.read(0x404,4)==0U&&
       m.read(0x408,4)==0x0182c079U,"SQRT guest RAM and flags");
 const auto e=system.state();system.restore(saved);system.run(2,&b);system.run(3,&b);
 check(system.state()==e&&a==b,"SQRT full System replay");
}
}
int main(){try{vectors();flow();guest();std::cout<<"FPU square-root tests passed\n";}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
