#include "critterlink/system.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class F> void rejected(F fn) {try{fn();}catch(const std::invalid_argument&){return;}throw std::runtime_error("expected rejection");}
constexpr std::uint32_t op(unsigned major,unsigned imm=0) {return (major<<26U)|(1U<<21U)|(2U<<16U)|imm;}
CpuState initial() {
    CpuState s;s.architectural_memory=true;s.pc=0xa0001000;s.next_pc=s.pc+4;
    s.cache.config=0x10447;s.cop0.status=0x20000000;s.gpr[1].low=0x80000200;return s;
}
void run(Cpu& cpu,Memory& memory,CpuState s,std::uint32_t instruction) {
    memory.write(0x1000,4,instruction);s.pc=0xa0001000;s.next_pc=s.pc+4;cpu.restore(s);
    check(cpu.step(memory).retired,"UCAB guest instruction failed");
}
void buffer() {
    Memory memory;CacheState s;
    for(unsigned n=0;n<256;++n) memory.write(0x200+n,1,n);
    check(accelerated_read(s,memory,0x278,8)==0x7f7e7d7c7b7a7978ULL,"aligned 128-byte refill tail");
    check(s.accelerated.valid && s.accelerated.base==0x200,"physical line tag");
    for(unsigned n=0;n<128;++n) memory.write(0x200+n,1,0xff);
    check(accelerated_read(s,memory,0x200,8)==0x0706050403020100ULL,"refill includes bytes preceding miss");
    check(accelerated_read(s,memory,0x240,8)==0x4746454443424140ULL,"entire 128-byte line cached");
    check(accelerated_read(s,memory,0x280,8)==0x8786858483828180ULL,"next line replaces buffer");
    check(accelerated_read(s,memory,0x200,8)==~std::uint64_t{0},"old line refills after replacement");
    auto before=s;rejected([&]{accelerated_read(s,memory,0x10000000,4);});check(s==before,"MMIO refill rejection atomic");
    rejected([&]{accelerated_read(s,memory,0x27f,2);});check(s==before,"cross-line rejection atomic");
    std::array<std::uint8_t,129> rom{};rom[0]=0x42;memory.load_boot_rom(rom);
    check(accelerated_read(s,memory,Memory::boot_rom_base,1)==0x42,"ROM refill");
    before=s;bool failed=false;try{accelerated_read(s,memory,Memory::boot_rom_base+128,1);}catch(const MemoryFault&){failed=true;}
    check(failed && s==before,"partial ROM line does not install partial refill");
    auto invalid=s;invalid.accelerated.base|=1;rejected([&]{validate_cache(invalid);});
    invalid=s;invalid.accelerated.base=0x80000000;rejected([&]{validate_cache(invalid);});
}
void loads() {
    Memory memory;Cpu cpu;memory.write(0x200,8,0x8877665544332211ULL);memory.write(0x208,8,0xffeeddccbbaa0099ULL);
    auto s=initial();run(cpu,memory,s,op(35));s=cpu.state();
    check(s.gpr[2].low==0x44332211 && s.cache.accelerated.valid,"mode-7 LW fill");
    memory.write(0x200,8,0);memory.write(0x208,8,0);
    for(const auto [major,imm,expected]:std::array<std::array<std::uint64_t,3>,8>{{
        {36,0,0x11},{37,0,0x2211},{35,0,0x44332211},{55,0,0x8877665544332211ULL},
        {34,1,0x22110000},{38,1,0x443322},{26,1,0x2211000000000000ULL},{27,1,0x88776655443322ULL}}}) {
        auto next=s;next.gpr[2]={};run(cpu,memory,next,op(static_cast<unsigned>(major),static_cast<unsigned>(imm)));
        check(cpu.state().gpr[2].low==expected && cpu.state().cache.accelerated==s.cache.accelerated,"scalar/merge loads use resident UCAB");
    }
    run(cpu,memory,s,op(30));check(cpu.state().gpr[2]==Register128{0x8877665544332211ULL,0xffeeddccbbaa0099ULL},"LQ uses both buffered lanes");
    run(cpu,memory,s,op(49));check(cpu.state().fpu.fpr[2]==0x44332211,"LWC1 uses resident UCAB");
    // Physical tags permit the same RAM line through a distinct mapped virtual page.
    auto mapped=s;mapped.gpr[1].low=0x00400200;mapped.mmu.entries[0]={0,0x00400000,0x3f,0x7f};
    run(cpu,memory,mapped,op(35));check(cpu.state().gpr[2].low==0x44332211,"TLB mode-7 alias hits physical UCAB tag");
    auto disabled=s;disabled.cache.config&=~0x10000U;run(cpu,memory,disabled,op(35));
    check(cpu.state().gpr[2].low==0 && !cpu.state().cache.accelerated.valid,"DCE override bypasses and invalidates UCAB");
    // Instruction fetch is not a data load and never consumes UCAB.
    auto fetch=s;fetch.pc=0x80001000;fetch.next_pc=fetch.pc+4;memory.write(0x1000,4,0x24030009);cpu.restore(fetch);
    check(cpu.step(memory).retired && cpu.state().gpr[3].low==9 && cpu.state().cache.accelerated==s.cache.accelerated,"mode-7 fetch preserves data buffer");
}
void invalidation() {
    Memory memory;Cpu cpu;auto s=initial();memory.write(0x200,4,42);run(cpu,memory,s,op(35));s=cpu.state();
    for(unsigned stype=0;stype<32;++stype) {
        run(cpu,memory,s,(stype<<6U)|15U);
        check(cpu.state().cache.accelerated.valid==((stype&16U)!=0),"all SYNC stypes L invalidation/P preservation");
    }
    for(auto address:{0xa0000200U,0xa0000280U}) {
        auto next=s;next.gpr[1].low=address;run(cpu,memory,next,op(35));
        check(!cpu.state().cache.accelerated.valid,"uncached loads invalidate even at same physical address");
    }
    auto cached=s;cached.cache.config=0x10443;run(cpu,memory,cached,op(35));
    check(!cpu.state().cache.accelerated.valid && !cpu.state().cache.data.empty(),"data-cache loads invalidate UCAB");
    auto scratch=s;scratch.gpr[1].low=0x70000000;scratch.mmu.entries[0]={0,0x70000000,0x80000017,0x17};
    run(cpu,memory,scratch,op(30));check(!cpu.state().cache.accelerated.valid,"scratchpad LQ invalidates UCAB");
    run(cpu,memory,scratch,op(31));check(!cpu.state().cache.accelerated.valid,"scratchpad SQ invalidates UCAB");
    run(cpu,memory,cached,op(43));check(!cpu.state().cache.accelerated.valid,"cached store invalidates UCAB");
    for(unsigned major:{40U,41U,43U,63U,42U,46U,44U,45U,31U,57U}) {
        auto next=s;next.gpr[1].low=0x80000300;run(cpu,memory,next,op(major));
        check(!cpu.state().cache.accelerated.valid,"every scalar/merge/quadword/FPU store invalidates UCAB");
    }
    run(cpu,memory,s,0);check(cpu.state().cache.accelerated==s.cache.accelerated,"ALU operations preserve UCAB");
    // Original exception handlers see an invalid buffer. Unsupported emulator stops do not retire.
    for(auto instruction:{12U,13U,op(35,1)}) {
        memory.write(0x1000,4,instruction);auto next=s;next.pc=0xa0001000;next.next_pc=next.pc+4;cpu.restore(next);
        const auto t=cpu.step(memory);check(t.exception && !t.retired && !cpu.state().cache.accelerated.valid,"exception entry invalidates UCAB");
    }
    memory.write(0x1000,4,0x40023800);s.pc=0xa0001000;s.next_pc=s.pc+4;cpu.restore(s);
    auto t=cpu.step(memory);check(t.stop && cpu.state().cache.accelerated==s.cache.accelerated,"unsupported instruction preserves buffer");
    auto fault=s;fault.gpr[1].low=0x00400000;memory.write(0x1000,4,op(35));cpu.restore(fault);
    t=cpu.step(memory);check(t.exception==2U && !cpu.state().cache.accelerated.valid,"TLB refill entry invalidates UCAB");
    memory.write(0x10000020,4,1);memory.write(0x10000010,4,0x180);memory.write(0x1000f010,4,0x200);memory.advance(1);
    auto irq=s;irq.cop0.status=0x10401;cpu.restore(irq);t=cpu.step(memory);
    check(t.exception==0U && !t.instruction && !cpu.state().cache.accelerated.valid,"interrupt invalidates before fetch");
    // A not-taken likely branch annuls a store; no speculative invalidation.
    memory.write(0x1000,4,0x54000001);memory.write(0x1004,4,op(43));cpu.restore(s);
    check(cpu.step(memory).retired && cpu.state().pc==0xa0001008 && cpu.state().cache.accelerated==s.cache.accelerated,"annulled store preserves UCAB");
    auto invalid=s;invalid.cache.accelerated.base=1;cpu.restore(s);rejected([&]{cpu.restore(invalid);});check(cpu.state()==s,"snapshot validation atomic");
    cpu.reset_boot_vector();check(cpu.state().cache.accelerated==AcceleratedBuffer{},"reset clears UCAB");
}
void replay() {
    System system;auto& memory=system.memory();auto s=initial();system.cpu().restore(s);
    // LW old; LW old; SYNC.L; LW new; SW output. Backing mutation occurs at checkpoint.
    const std::array<std::uint32_t,5> words{op(35),op(35),15,op(35),op(43,0x100)};
    for(unsigned n=0;n<words.size();++n) memory.write(0x1000+n*4,4,words[n]);
    memory.write(0x200,4,7);check(system.run(1).retired==1,"UCAB replay fill");
    memory.write(0x200,4,19);const auto saved=system.state();
    std::vector<InstructionTrace> a,b;check(system.run(1,&a).retired==1 && system.cpu().state().gpr[2].low==7,"guest observes stale UCAB");
    system.run(3,&a);const auto end=system.state();
    check(memory.read(0x300,4)==19 && !end.cpu.cache.accelerated.valid,"guest SYNC refresh and store invalidate");
    system.restore(saved);system.run(1,&b);system.run(3,&b);
    check(system.state()==end && a==b,"UCAB full System checkpoint replay");
}
}
int main(){try{buffer();loads();invalidation();replay();std::cout<<"Accelerated read-buffer tests passed\n";}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
