#include "critterlink/system.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void rejected(F fn) {
    try { fn(); } catch(const std::invalid_argument&) { return; }
    throw std::runtime_error("expected invalid-argument rejection");
}
template<class F> void fault(F fn,unsigned code,std::uint32_t address,bool refill=false) {
    try { fn(); } catch(const TranslationFault& f) {
        check(f.code==code && f.address==address && f.refill==refill,"translation exception metadata");return;
    }
    throw std::runtime_error("missing translation exception");
}
void map(MmuState& s,unsigned index,std::uint32_t hi,std::uint32_t lo0,std::uint32_t lo1,std::uint32_t mask=0) {
    s.hi=hi;s.lo0=lo0;s.lo1=lo1;s.mask=mask;tlb_write(s,index);
}
void mappings() {
    MmuState s;validate_mmu(s);
    const std::array<std::uint32_t,7> masks{0,0x6000,0x1e000,0x7e000,0x1fe000,0x7fe000,0x1ffe000};
    const std::array<std::uint32_t,7> sizes{4096,16384,65536,262144,1048576,4194304,16777216};
    for(unsigned n=0;n<masks.size();++n) {
        // Distinct 64-MiB-aligned physical frames; ignored PFN low bits are set.
        map(s,0,0x40000007,0x001000d6,0x00200116,masks[n]);
        for(auto offset : {0U,sizes[n]-1}) {
            const auto a=translate(s,0,0x40000000+offset,Access::load);
            const auto b=translate(s,0,0x40000000+sizes[n]+offset,Access::store);
            const auto even_base=0x04003000U&~(sizes[n]-1);
            const auto odd_base=0x08004000U&~(sizes[n]-1);
            check(a.address==even_base+offset && b.address==odd_base+offset && a.mapped && !a.global,"page size/even-odd frames");
        }
        fault([&]{translate(s,0,0x40000000+2*sizes[n],Access::load);},2,0x40000000+2*sizes[n],true);
        s.hi=8;fault([&]{translate(s,0,0x40000000,Access::load);},2,0x40000000,true);
        map(s,0,0x40000007,0x001000d7,0x00200117,masks[n]);s.hi=8;
        check(translate(s,0,0x40000000,Access::load).global,"global ignores ASID");
    }
    map(s,0,0x00400007,0x96,0xd6);s.hi=7;
    check(translate(s,0,0x00401234,Access::load).address==0x3234,"4-KiB odd mapping");
    s.entries[0].lo1=0xd0;
    fault([&]{translate(s,0,0x00401234,Access::fetch);},2,0x00401234);
    fault([&]{translate(s,0,0x00401234,Access::store);},3,0x00401234);
    s.entries[0].lo1=0xd2;
    fault([&]{translate(s,0,0x00401234,Access::store);},1,0x00401234);
    for(auto address:{0U,0x12345678U,0x7fffffffU})
        check(translate(s,4,address,Access::load).address==address,"ERL KUSEG identity");
    check(translate(s,0,0x80012345,Access::load).address==0x12345 &&
          translate(s,0,0xbfc00000,Access::fetch).address==0x1fc00000,"kernel direct mappings");
    for(auto address:{0x80000000U,0xa0000000U,0xc0000000U,0xe0000000U}) {
        fault([&]{translate(s,0x10,address,Access::fetch);},4,address);
        fault([&]{translate(s,0x10,address,Access::store);},5,address);
    }
    map(s,2,0xc0000007,0x16,0x56);
    check(translate(s,8,0xc0000123,Access::load).address==0x123,"supervisor mapped segment");
    fault([&]{translate(s,8,0x80000000,Access::load);},4,0x80000000);
    fault([&]{translate(s,8,0xe0000000,Access::load);},4,0xe0000000);
    check(translate(s,0x12,0x80000000,Access::load).address==0,"EXL overrides KSU");
    map(s,1,0x70000007,0x80000016,0x16);
    for(auto offset:{0U,0x1fffU,0x2000U,0x3fffU}) {
        const auto t=translate(s,0,0x70000000+offset,Access::store);
        check(t.scratchpad && t.address==offset,"scratchpad full 16-KiB mapping");
    }
    fault([&]{translate(s,0,0x70004000,Access::load);},2,0x70004000,true);
    rejected([&]{translate(s,0,0x70000000,Access::fetch);});
    s.entries[3]=s.entries[1];rejected([&]{translate(s,0,0x70000000,Access::load);});
}
void controls() {
    MmuState s;
    check(write_mmu_register(s,2,0xffffffff) && s.lo0==0x83ffffff,"EntryLo0 mask");
    write_mmu_register(s,3,0xffffffff);check(s.lo1==0x03ffffff,"EntryLo1 mask");
    write_mmu_register(s,10,0xffffffff);check(s.hi==0xffffe0ff,"EntryHi mask");
    s.context=0x123450;write_mmu_register(s,4,0xff80000f);check(s.context==0xff923450,"Context writable base");
    for(unsigned i=0;i<48;++i) {
        map(s,i,0x10000000+i*0x2000+3,0x97,0xd6);
        s.index=i;tlb_read(s);check(s.lo0==0x96 && s.lo1==0xd6,"paired global AND on readback");
        tlb_probe(s);check(s.index==i,"all TLB slots probe");
    }
    s.hi=0x20000003;tlb_probe(s);check(s.index==0x80000000,"probe miss");
    s.index=48;rejected([&]{tlb_read(s);});rejected([&]{tlb_write(s,48);});
    s.mask=0x2000;rejected([&]{tlb_write(s,0);});
    s.mask=0;s.hi=0x70002000;s.lo0=0x80000016;s.lo1=0x16;
    rejected([&]{tlb_write(s,0);});s.hi=0x70000000;s.lo1=0x12;rejected([&]{tlb_write(s,0);});
    s.lo1=0x16;tlb_write(s,0);s.index=0;tlb_read(s);check(s.lo0==0x80000016 && s.lo1==0x16,"scratchpad readback");
    for(unsigned wired:{0U,1U,46U,47U}) {
        write_mmu_register(s,6,wired);check(s.random==47,"Wired resets Random");
        for(unsigned n=0;n<100;++n) {const auto prior=s.random;advance_random(s);check(s.random==(prior==wired?47:prior-1),"Random range/wrap");}
    }
    rejected([&]{write_mmu_register(s,6,48);});
    s=MmuState{};auto bad=s;bad.entries[0].hi|=1U<<8U;rejected([&]{validate_mmu(bad);});
    bad=s;bad.random=48;rejected([&]{validate_mmu(bad);});
    bad=s;bad.entries[0].lo0|=1;rejected([&]{validate_mmu(bad);});
}
CpuState execution(std::uint32_t status=0) {
    CpuState s;s.architectural_memory=true;s.pc=0x80001000;s.next_pc=0x80001004;s.cop0.status=status;return s;
}
void instructions() {
    Memory memory;Cpu cpu;
    for(unsigned reg:{0U,1U,2U,3U,4U,5U,6U,10U}) for(unsigned rt:{0U,3U,31U}) {
        auto s=execution();s.architectural_memory=false;
        s.mmu.index=0x80000001;s.mmu.random=23;s.mmu.wired=7;s.mmu.lo0=0x80000016;
        s.mmu.lo1=0x96;s.mmu.context=0xab802000;s.mmu.mask=0x6000;s.mmu.hi=0x70000007;
        s.gpr[rt].high=rt==0?0:0xfeedface;
        const auto value=*read_mmu_register(s.mmu,reg);
        const auto expected=(value&0x80000000U)!=0?0xffffffff00000000ULL|value:std::uint64_t{value};
        memory.write(0x1000,4,0x40000000U|(rt<<16U)|(reg<<11U));cpu.restore(s);
        check(cpu.step(memory).retired && cpu.state().gpr[rt].low==(rt==0?0:expected) &&
              cpu.state().gpr[rt].high==s.gpr[rt].high && cpu.state().mmu==s.mmu,"MFC0 MMU registers");
    }
    for(auto instruction:{0x42000002U,0x42000006U}) {
        auto s=execution();s.mmu.index=5;s.mmu.random=17;s.mmu.wired=2;
        s.mmu.hi=0x00400007;s.mmu.lo0=0x97;s.mmu.lo1=0xd7;
        memory.write(0x1000,4,instruction);cpu.restore(s);check(cpu.step(memory).retired,"TLB write opcode");
        const auto index=instruction==0x42000002U?5U:17U;
        check(cpu.state().mmu.entries[index]==TlbEntry{0,0x00400007,0x97,0xd7} && cpu.state().mmu.random==16,"TLBWI/TLBWR selection");
        auto next=cpu.state();next.pc=0x80001000;next.next_pc=0x80001004;next.mmu.index=index;
        next.mmu.lo0=0;next.mmu.lo1=0;memory.write(0x1000,4,0x42000001);cpu.restore(next);
        check(cpu.step(memory).retired && cpu.state().mmu.lo0==0x97 && cpu.state().mmu.lo1==0xd7,"TLBR opcode");
        next=cpu.state();next.pc=0x80001000;next.next_pc=0x80001004;next.mmu.index=0;
        memory.write(0x1000,4,0x42000008);cpu.restore(next);
        check(cpu.step(memory).retired && cpu.state().mmu.index==index,"TLBP opcode");
    }
    auto s=execution();s.gpr[1].low=46;memory.write(0x1000,4,0x40813000);cpu.restore(s);
    check(cpu.step(memory).retired && cpu.state().mmu.wired==46 && cpu.state().mmu.random==47,"guest Wired reset");
    memory.write(0x1004,4,0);memory.write(0x1008,4,0);
    check(cpu.step(memory).retired && cpu.state().mmu.random==46 && cpu.step(memory).retired && cpu.state().mmu.random==47,"guest Random wrap");
    for(auto opcode:{0x42000001U,0x42000002U}) {
        s=execution();s.mmu.index=48;memory.write(0x1000,4,opcode);cpu.restore(s);auto t=cpu.step(memory);
        s.stop=t.stop;check(t.stop && !t.retired && cpu.state()==s,"invalid TLB operation atomicity");
    }
    // Invalid source masks can be staged in PageMask, but cannot silently map.
    s=execution();s.mmu.mask=0x2000;memory.write(0x1000,4,0x42000002);cpu.restore(s);
    check(cpu.step(memory).stop.has_value(),"reserved page size rejected on TLB write");
}
void exceptions() {
    Memory memory;Cpu cpu;
    for(bool store:{false,true}) for(bool bev:{false,true}) for(bool nested:{false,true}) for(bool slot:{false,true})
    for(unsigned kind=0;kind<3;++kind) {
        if(kind==2 && !store) continue;
        auto s=execution((bev?0x400000U:0U)|(nested?2U:0U));s.delay_slot=slot;s.branch_pc=0x80000ffc;
        s.cop0.epc=0x1234;s.cop0.cause=nested?0x80000000U:0U;s.mmu.hi=7;s.mmu.context=0xab800000;
        s.gpr[2].low=0x00401234;s.gpr[1].low=0x12345678;
        if(kind!=0) map(s.mmu,0,0x00400007,0x96,kind==1?0xd0U:0xd2U);
        memory.write(0x1000,4,store?0xac410000:0x8c410000);cpu.restore(s);auto t=cpu.step(memory);
        const auto code=kind==2?1U:store?3U:2U;const auto base=bev?0xbfc00200U:0x80000000U;
        check(t.exception==code && !t.retired && !t.stop && cpu.state().pc==base+((kind==0&&!nested)?0:0x180) &&
              cpu.state().cop0.bad_vaddr==0x00401234 && cpu.state().mmu.hi==0x00400007 &&
              cpu.state().mmu.context==0xab802000 && cpu.state().gpr==s.gpr &&
              cpu.state().cop0.epc==(nested?0x1234U:slot?0x80000ffcU:0x80001000U) &&
              (cpu.state().cop0.cause&0x80000000U)==((nested||slot)?0x80000000U:0U),"CPU TLB exception entry");
    }
    auto s=execution();s.gpr[2].low=0x00400001;memory.write(0x1000,4,0x8c410000);cpu.restore(s);
    check(cpu.step(memory).exception==4U && cpu.state().mmu==s.mmu,"alignment before translation");
    for(unsigned opcode:{26U,27U,34U,38U,42U,44U,45U,46U}) {
        s=execution();s.gpr[2].low=0x00401237;
        memory.write(0x1000,4,(opcode<<26U)|(2U<<21U)|(1U<<16U));cpu.restore(s);
        const auto code=opcode==26 || opcode==27 || opcode==34 || opcode==38?2U:3U;
        check(cpu.step(memory).exception==code && cpu.state().cop0.bad_vaddr==0x00401237,
              "merge translation fault retains effective address");
    }
    s=execution();s.pc=0x00400000;s.next_pc=s.pc+4;cpu.restore(s);
    check(cpu.step(memory).exception==2U && cpu.state().cop0.bad_vaddr==0x00400000,"instruction refill");
    // User fetch is mapped, but COP0 needs CU0; EXL/ERL override user privilege.
    s=execution(0x10);s.pc=0x00400000;s.next_pc=s.pc+4;map(s.mmu,0,0x00400000,0x56,0x96);
    memory.write(0x1000,4,0x40026000);cpu.restore(s);
    check(cpu.step(memory).exception==11U && (cpu.state().cop0.cause&0x30000000U)==0,"COP0 unusable");
    s.cop0.status|=0x10000000;cpu.restore(s);check(cpu.step(memory).retired && cpu.state().gpr[2].low==0x10000010,"CU0 enabled access");
    auto bad=s;bad.mmu.random=48;const auto before=cpu.state();rejected([&]{cpu.restore(bad);});check(cpu.state()==before,"MMU restore atomicity");
}
void guest() {
    System system;std::vector<std::uint32_t> program;
    auto set=[&](unsigned r,std::uint32_t value){program.push_back(0x3c000000U|(r<<16U)|(value>>16U));program.push_back(0x34000000U|(r<<21U)|(r<<16U)|(value&0xffffU));};
    auto control=[&](unsigned r,std::uint32_t value){set(1,value);program.push_back(0x40810000U|(r<<11U));};
    control(0,0);control(10,0x00400007);control(2,0x96);control(3,0xd6);control(5,0);
    program.push_back(0x42000002);program.push_back(0x0000040f); // TLBWI, SYNC.P
    control(0,1);control(10,0x70000007);control(2,0x80000016);control(3,0x16);
    program.push_back(0x42000002);program.push_back(0x0000040f);
    set(1,0x00400000);set(2,0x12345678);program.push_back(0xac220000);
    set(1,0x00401000);program.push_back(0xac220000);
    set(1,0x70003ffc);program.push_back(0xac220000);program.push_back(0x8c230000);
    for(unsigned n=0;n<program.size();++n)system.memory().write(0x1000+n*4,4,program[n]);
    system.cpu().restore(execution());check(system.run(17).retired==17,"TLB guest setup");
    const auto saved=system.state();std::vector<InstructionTrace>a,b;
    const auto remaining=program.size()-17;check(system.run(remaining,&a).retired==remaining,"TLB guest execution");
    check(system.memory().read(0x2000,4)==0x12345678 && system.memory().read(0x3000,4)==0x12345678 &&
          system.memory().read_scratchpad(0x3ffc,4)==0x12345678 && system.cpu().state().gpr[3].low==0x12345678,"TLB guest independent output");
    const auto end=system.state();
    auto invalid=saved;invalid.memory.scratchpad.pop_back();rejected([&]{system.restore(invalid);});
    check(system.state()==end,"scratchpad snapshot size rejection is atomic");
    system.restore(saved);system.run(remaining,&b);check(system.state()==end && a==b,"TLB/scratchpad System replay");
    // Quadword path through a non-default scratchpad virtual mapping.
    auto s=execution();map(s.mmu,0,0x60000000,0x80000016,0x16);s.gpr[1].low=0x60003ff0;s.gpr[2]={0x0123456789abcdefULL,0xfedcba9876543210ULL};
    system.cpu().restore(s);system.memory().write(0x1000,4,0x7c220000);system.memory().write(0x1004,4,0x78230000);
    check(system.cpu().step(system.memory()).retired && system.cpu().step(system.memory()).retired && system.cpu().state().gpr[3]==s.gpr[2],"scratchpad SQ/LQ");
    rejected([&]{system.memory().write_scratchpad(0x3fff,2,0);});
    check(system.memory().read_scratchpad(0x3fff,1)==0xfe,"scratchpad rejected write atomicity");
    s=execution();map(s.mmu,0,0x60000000,0x80000016,0x16);s.gpr[1].low=0x60000001;
    s.gpr[4].low=0xfedcba9876543210ULL;system.cpu().restore(s);
    system.memory().write_scratchpad(0,4,0x33221100);system.memory().write(0x1000,4,0x98240000);
    check(system.cpu().step(system.memory()).retired && system.cpu().state().gpr[4].low==0xfedcba9876332211ULL,
          "mapped scratchpad partial LWR");
}
void refill_handler() {
    System system;
    // Original handler maps the missing pair from staged EntryLo registers,
    // barriers, then retries the interrupted LW via ERET.
    system.memory().write(0,4,0x42000002);system.memory().write(4,4,0x0000040f);system.memory().write(8,4,0x42000018);
    system.memory().write(0x1000,4,0x8c410000);system.memory().write(0x2234,4,0x87654321);
    auto s=execution();s.gpr[2].low=0x00400234;s.mmu.lo0=0x96;s.mmu.lo1=0xd6;s.mmu.hi=7;
    system.cpu().restore(s);std::vector<InstructionTrace>a,b;
    check(system.run(1,&a).retired==0 && a[0].exception==2U,"guest refill entry");
    const auto saved=system.state();a.clear();check(system.run(4,&a).retired==4,"guest refill service/retry");
    check(system.cpu().state().gpr[1].low==0xffffffff87654321ULL && system.cpu().state().pc==0x80001004 &&
          system.cpu().state().cop0.status==0 && system.memory().hardware().now()==5,"refill handler literal result");
    const auto end=system.state();system.restore(saved);system.run(4,&b);
    check(system.state()==end && a==b,"refill-handler checkpoint replay");
}
}
int main(){try{mappings();controls();instructions();exceptions();guest();refill_handler();std::cout<<"MMU translation and guest tests passed\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
