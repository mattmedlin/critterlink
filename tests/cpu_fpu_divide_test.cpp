#include "critterlink/system.hpp"
#include "cpu_fpu_divide_vectors.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using namespace critterlink;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
constexpr std::uint32_t op(unsigned fs,unsigned ft,unsigned fd){return 0x46000003U|(ft<<16U)|(fs<<11U)|(fd<<6U);}
CpuState initial(){CpuState s;s.cop0.status=0x20000000;s.fpu.control=0x0180c019;s.fpu.accumulator=0xdeadbeef;return s;}
void result(Memory& m,Cpu& cpu,std::uint32_t a,std::uint32_t b,std::uint32_t bits,std::uint32_t flags,unsigned fd=2){
 auto s=initial();s.fpu.fpr[0]=a;s.fpu.fpr[31]=b;auto expected=s;
 expected.fpu.fpr[fd]=bits;expected.fpu.control|=flags;expected.pc=4;expected.next_pc=8;
 m.write(0,4,op(0,31,fd));cpu.restore(s);auto t=cpu.step(m);
 check(t.retired&&!t.stop&&!t.exception&&cpu.state()==expected,"DIV result, flags or preserved state");
}
void vectors(){
 Memory m;Cpu cpu;
 for(const auto& v:fpu_divide_vectors)for(unsigned fd:{0U,2U,31U})result(m,cpu,v[0],v[1],v[2],v[3],fd);
 for(unsigned sign:{0U,0x80000000U}){
  result(m,cpu,0x3f800000U|sign,0x40400000,0x3eaaaaabU|sign,0);
  result(m,cpu,0x3f800000U|sign,0x3fc00000,0x3f2aaaabU|sign,0);
  result(m,cpu,0x7fffffffU|sign,0x00800000,0x7fffffffU|sign,0);
  result(m,cpu,0x00800000U|sign,0x7fffffff,sign,0);
  result(m,cpu,0x00800000U|sign,0x3f800000,0x00800000U|sign,0);
  result(m,cpu,0x7f800000U|sign,0x3f800000,0x7f800000U|sign,0);
 }
 // Every input exponent with exact power-of-two quotients and both sign combinations.
 for(unsigned a=1;a<256;++a)for(unsigned b=1;b<256;++b){
  const int exponent=static_cast<int>(a)-static_cast<int>(b)+127;
  const auto bits=exponent<=0?0U:exponent>255?0x7fffffffU:static_cast<std::uint32_t>(exponent)<<23U;
  result(m,cpu,a<<23U,b<<23U,bits,0);
  result(m,cpu,(a<<23U)|0x80000000U,b<<23U,bits|0x80000000U,0);
 }
 for(unsigned fs=0;fs<32;++fs)for(unsigned ft=0;ft<32;++ft)for(unsigned fd:{0U,fs,ft,31U}){
  auto s=initial();s.fpu.fpr[fs]=0x3f800000;s.fpu.fpr[ft]=0x40400000;auto e=s;
  e.fpu.fpr[fd]=fs==ft?0x3f800000U:0x3eaaaaabU;e.pc=4;e.next_pc=8;
  m.write(0,4,op(fs,ft,fd));cpu.restore(s);check(cpu.step(m).retired&&cpu.state()==e,"DIV register aliases");
 }
 // Current I/D clear independently of sticky bits, O/U, and C.
 auto s=initial();s.fpu.control=0x0183c079;s.fpu.fpr[0]=0x3f800000;auto e=s;
 e.fpu.fpr[1]=0x3f800000;e.fpu.control=0x0180c079;e.pc=4;e.next_pc=8;
 m.write(0,4,op(0,0,1));cpu.restore(s);check(cpu.step(m).retired&&cpu.state()==e,"DIV flag clearing");
}
void flow(){
 Memory m;Cpu cpu;
 for(bool slot:{false,true}){
  auto s=initial();s.cop0.status=0;s.pc=slot?4U:0U;s.next_pc=8;s.delay_slot=slot;
  m.write(s.pc,4,op(0,31,0));cpu.restore(s);auto t=cpu.step(m);
  check(t.exception==11U&&!t.retired&&!t.stop&&cpu.state().fpu==s.fpu&&
        cpu.state().cop0.cause==(slot?0x9000002cU:0x1000002cU),"DIV CU1 priority");
 }
 for(auto branch:{0x10000003U,0x14000003U,0x54000003U}){
  auto s=initial();s.fpu.fpr[0]=0x3f800000;s.fpu.fpr[31]=0x40400000;
  m.write(0,4,branch);m.write(4,4,op(0,31,0));cpu.restore(s);check(cpu.step(m).retired,"DIV branch setup");
  if(branch==0x54000003U)check(cpu.state().pc==8&&cpu.state().fpu==s.fpu,"DIV annul");
  else{const auto saved=cpu.state();auto t=cpu.step(m);const auto e=cpu.state();cpu.restore(saved);
   check(t.retired&&t.delay_slot&&cpu.step(m)==t&&cpu.state()==e,"DIV slot replay");}
 }
 for(unsigned format:{0U,4U,20U,31U}){
  auto s=initial();m.write(0,4,0x44000003U|(format<<21U));cpu.restore(s);auto t=cpu.step(m);s.stop=cpu.state().stop;
  check(t.stop&&!t.retired&&!t.exception&&cpu.state()==s,"DIV unsupported format");
 }
}
void guest(){
 System system;auto& m=system.memory();
 const std::array<std::uint32_t,7> program{op(0,1,2),op(0,3,4),0x4445f800,0xe4020400,0xe4040404,0xac050408,0};
 for(std::size_t i=0;i<program.size();++i)m.write(static_cast<std::uint32_t>(i*4),4,program[i]);
 auto s=initial();s.fpu.fpr[0]=0x3f800000;s.fpu.fpr[1]=0x40400000;system.cpu().restore(s);
 check(system.run(1)==RunResult{1,true},"DIV guest prefix");const auto saved=system.state();std::vector<InstructionTrace>a,b;
 check(system.run(5,&a)==RunResult{5,true}&&m.read(0x400,4)==0x3eaaaaabU&&m.read(0x404,4)==0x7fffffffU&&
       m.read(0x408,4)==0x0181c039U,"DIV guest RAM results");
 const auto e=system.state();system.restore(saved);system.run(2,&b);system.run(3,&b);
 check(system.state()==e&&a==b,"DIV full System replay");
}
}
int main(){try{vectors();flow();guest();std::cout<<"FPU division tests passed\n";}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
