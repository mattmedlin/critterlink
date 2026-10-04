#include "critterlink/system.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class F> void rejected(F fn) {try{fn();}catch(const std::invalid_argument&){return;}throw std::runtime_error("expected rejection");}
void behavior() {
    Memory memory;CacheState cache;validate_cache(cache);
    check(cache.config==0x442 && !cache_enabled(cache,3,true) && !cache_enabled(cache,3,false),"disabled reset caches");
    write_config(cache,0xffffffff);check(cache.config==0x73447,"Config masks");
    for(auto mode:{1U,4U,5U,6U}) rejected([&]{write_config(cache,mode);});
    write_config(cache,0x30003);check(cache_enabled(cache,3,true) && cache_enabled(cache,3,false) && !cache_enabled(cache,2,false),"cache enables");
    memory.write(0x100,4,0x11223344);
    check(cache_read(cache,memory,0x80000100,0x100,4,false)==0x11223344,"data-cache fill");
    memory.write(0x100,4,0x55667788);
    check(cache_read(cache,memory,0x80000100,0x100,4,false)==0x11223344,"external writes do not snoop cache");
    cache_write(cache,memory,0x80000100,0x100,4,0xaabbccdd,3);
    check(memory.read(0x100,4)==0x55667788 && cache_read(cache,memory,0x80000100,0x100,4,false)==0xaabbccdd,"dirty data remains cached");
    cache_operation(cache,memory,0x1c,0x80000100,0x100);
    check(memory.read(0x100,4)==0xaabbccdd,"hit writeback without invalidate");
    memory.write(0x100,4,0x01020304);check(cache_read(cache,memory,0x80000100,0x100,4,false)==0xaabbccdd,"writeback retains valid line");
    cache_operation(cache,memory,0x1a,0x80000100,0x100);
    check(cache_read(cache,memory,0x80000100,0x100,4,false)==0x01020304,"hit invalidation refills");
    cache_write(cache,memory,0x80000100,0x100,4,0x87654321,3);cache_operation(cache,memory,0x18,0x80000100,0x100);
    check(memory.read(0x100,4)==0x87654321,"hit writeback invalidate");
    // LRF evicts way 0 after two fills, even if way 0 was subsequently hit.
    cache=CacheState{};memory.write(0x100,4,10);memory.write(0x1100,4,20);memory.write(0x2100,4,30);
    cache_write(cache,memory,0x80000100,0x100,4,11,3);
    check(cache_read(cache,memory,0x80001100,0x1100,4,false)==20,"second-way fill");
    check(cache_read(cache,memory,0x80000100,0x100,4,false)==11,"first-way hit");
    check(cache_read(cache,memory,0x80002100,0x2100,4,false)==30 && memory.read(0x100,4)==11,"LRF dirty eviction");
    check(cache.data[8].tag==0x2020 && cache.data[9].tag==0x1030,"LRF literal tags");
    // Write-through/no-write-allocate mode does not allocate a store miss.
    cache=CacheState{};cache_write(cache,memory,0x80000400,0x400,4,42,0);
    check(memory.read(0x400,4)==42 && cache.data[32].tag==0 && cache.data[33].tag==0,"no-write-allocate miss");
    cache_read(cache,memory,0x80000400,0x400,4,false);cache_write(cache,memory,0x80000400,0x400,4,43,0);
    check(memory.read(0x400,4)==43 && (cache.data[32].tag&0x40U)==0,"write-through hit");
    // I-cache has independent data and requires invalidation after code changes.
    cache=CacheState{};memory.write(0x2000,4,0x24020001);
    check(cache_read(cache,memory,0x80002000,0x2000,4,true)==0x24020001,"instruction fill");
    memory.write(0x2000,4,0x24020002);
    check(cache_read(cache,memory,0x80002000,0x2000,4,true)==0x24020001,"instruction staleness");
    cache_operation(cache,memory,0xb,0x80002000,0x2000);
    check(cache_read(cache,memory,0x80002000,0x2000,4,true)==0x24020002,"instruction invalidate visibility");
}
void maintenance() {
    Memory memory;CacheState cache;
    for(unsigned instruction=0;instruction<2;++instruction) for(unsigned index=0;index<(instruction?128U:64U);++index)
    for(unsigned way=0;way<2;++way) {
        const auto va=(index<<6U)|way;cache.tag_lo=0x12345030;
        cache_operation(cache,memory,instruction?4U:0x12U,va);
        cache.tag_lo=0;cache_operation(cache,memory,instruction?0U:0x10U,va);
        check(cache.tag_lo==0x12345030,"all indices/ways tag roundtrip");
        if(!instruction) for(unsigned word=0;word<16;++word) {
            cache.tag_lo=0x10203040+word;cache_operation(cache,memory,0x13,va+word*4);
            cache.tag_lo=0;cache_operation(cache,memory,0x11,va+word*4);
            check(cache.tag_lo==0x10203040+word,"indexed data roundtrip");
        }
        cache_operation(cache,memory,instruction?7U:0x16U,va);
        cache_operation(cache,memory,instruction?0U:0x10U,va);
        check(cache.tag_lo==0x12345010,"index invalidate preserves LRF/PFN");
    }
    cache=CacheState{};cache.tag_lo=0x10038;cache_operation(cache,memory,0x12,0);
    cache_write(cache,memory,0x80010000,0x10000,4,99,3);
    check(memory.read(0x10000,4)==0 && cache_read(cache,memory,0x80010000,0x10000,4,false)==99 &&
          (cache.data[0].tag&0x40U)==0,"locked line writes do not set dirty");
    cache_read(cache,memory,0x80011000,0x11000,4,false);cache_read(cache,memory,0x80012000,0x12000,4,false);
    check(cache.data[0].tag==0x10038,"locked way excluded from replacement");
    cache.tag_lo=0x11028;rejected([&]{cache_operation(cache,memory,0x12,1);});
    cache.tag_lo=0x10038;rejected([&]{cache_operation(cache,memory,0x12,0);});
    auto invalid=cache;invalid.data[1].tag|=8;rejected([&]{validate_cache(invalid);});
    invalid=cache;invalid.instruction.resize(255);rejected([&]{validate_cache(invalid);});
    invalid=cache;invalid.data[0].tag=0x40;rejected([&]{validate_cache(invalid);});
    for(auto op:{1U,5U,0xcU,0x1fU}) rejected([&]{cache_operation(cache,memory,op,0);});
}
CpuState cpu_state() {CpuState s;s.architectural_memory=true;s.pc=0xa0001000;s.next_pc=0xa0001004;return s;}
void controls() {
    Memory memory;Cpu cpu;
    for(unsigned reg:{16U,28U,29U}) {
        auto s=cpu_state();s.gpr[1].low=0xffffffff;s.gpr[2].high=0x12345678;
        memory.write(0x1000,4,0x40810000U|(reg<<11U));memory.write(0x1004,4,0x40020000U|(reg<<11U));cpu.restore(s);
        check(cpu.step(memory).retired && cpu.step(memory).retired && cpu.state().gpr[2].low==(reg==16?0x73447ULL:0xffffffffffffffffULL) &&
              cpu.state().gpr[2].high==0x12345678,"Config/Tag register transfers");
    }
    // Index operations do not consult the TLB or require aligned addresses.
    auto s=cpu_state();s.gpr[1].low=0x00400001;s.cache.tag_lo=0x12345030;
    memory.write(0x1000,4,0xbc320000);cpu.restore(s);
    check(cpu.step(memory).retired && cpu.state().cache.data[1].tag==0x12345030,"index operation without mapping");
    // Hit maintenance translates even with DCE disabled, and miss dispatches.
    s=cpu_state();s.gpr[1].low=0x00400000;memory.write(0x1000,4,0xbc380000);cpu.restore(s);
    check(cpu.step(memory).exception==2U && cpu.state().cop0.bad_vaddr==0x00400000,"hit CACHE translation fault");
    // User mode can fetch the opcode but cannot operate COP0 without CU0.
    s=cpu_state();s.pc=0x00400000;s.next_pc=s.pc+4;s.cop0.status=0x10;
    s.mmu.hi=0x00400000;s.mmu.lo0=0x56;s.mmu.lo1=0x96;tlb_write(s.mmu,0);
    memory.write(0x1000,4,0xbc160000);cpu.restore(s);
    check(cpu.step(memory).exception==11U && cpu.state().cache==s.cache,"CACHE CU0 gating");
    s=cpu_state();s.gpr[1].low=0xa0000000;memory.write(0x1000,4,0xbc380000);cpu.restore(s);
    check(cpu.step(memory).stop.has_value(),"uncached hit-maintenance address rejected");
    s=cpu_state();memory.write(0x1000,4,0xbc1f0000);cpu.restore(s);
    check(cpu.step(memory).stop.has_value() && cpu.state().cache==s.cache,"unsupported CACHE encoding remains explicit");
    s=cpu_state();s.cache.config=0x10443;s.gpr[1].low=0x90000000;
    memory.write(0x1000,4,0xac220000);cpu.restore(s);
    check(cpu.step(memory).stop->kind==StopKind::unsupported_access && cpu.state().cache==s.cache,
          "cached device access stops without allocating or mutating cache");
    // Cached SQ/LQ preserve both lanes and remain invisible before writeback.
    s=cpu_state();s.cache.config=0x10443;s.gpr[1].low=0x800003f0;
    s.gpr[2]={0x1122334455667788ULL,0x8877665544332211ULL};
    memory.write(0x1000,4,0x7c220000);memory.write(0x1004,4,0x78230000);cpu.restore(s);
    check(cpu.step(memory).retired && memory.read(0x3f0,8)==0 && cpu.step(memory).retired && cpu.state().gpr[3]==s.gpr[2],"cached SQ/LQ");
    // Partial writes preserve untouched bytes in a filled data line.
    auto c=CacheState{};memory.write(0x500,8,0x8877665544332211ULL);
    cache_write(c,memory,0x80000502,0x502,3,0xaabbcc,3);
    check(cache_read(c,memory,0x80000500,0x500,8,false)==0x887766aabbcc2211ULL && memory.read(0x500,8)==0x8877665544332211ULL,"cached byte enables");
    cache_operation(c,memory,0x14,0x500);check(memory.read(0x500,8)==0x887766aabbcc2211ULL,"index writeback uses stored physical tag");
}
void guest() {
    System system;auto& memory=system.memory();std::vector<std::uint32_t> words;
    auto load=[&](unsigned r,std::uint32_t value){words.push_back(0x3c000000U|(r<<16U)|(value>>16U));words.push_back(0x34000000U|(r<<21U)|(r<<16U)|(value&0xffffU));};
    load(1,0x10003);words.push_back(0x40818000); // MTC0 Config enables D-cache.
    load(2,0x80000200);load(3,0x12345678);words.push_back(0xac430000); // cached SW
    load(4,0xa0000200);words.push_back(0x8c850000); // uncached LW observes old RAM
    words.push_back(0x0000000f);words.push_back(0xbc580000);words.push_back(0x0000000f); // DHWBIN + barriers
    words.push_back(0x8c860000); // uncached LW observes writeback
    for(unsigned n=0;n<words.size();++n) memory.write(0x1000+n*4,4,words[n]);
    system.cpu().restore(cpu_state());check(system.run(8).retired==8,"cache guest prefix");
    check(memory.read(0x200,4)==0,"dirty guest store did not write RAM");
    const auto saved=system.state();std::vector<InstructionTrace>a,b;
    check(system.run(words.size()-8,&a).retired==words.size()-8,"cache guest finish");
    check(system.cpu().state().gpr[5].low==0 && system.cpu().state().gpr[6].low==0x12345678 && memory.read(0x200,4)==0x12345678,"guest writeback output");
    const auto end=system.state();system.restore(saved);system.run(words.size()-8,&b);
    check(system.state()==end && a==b,"dirty-cache System replay");
    auto invalid=end;invalid.cpu.cache.config=0;rejected([&]{system.restore(invalid);});check(system.state()==end,"cache snapshot rejection atomic");
    // Cached instruction fetch sees old code until an uncached handler invalidates it.
    auto s=cpu_state();s.cache.config=0x20443;s.pc=0x80002000;s.next_pc=s.pc+4;
    memory.write(0x2000,4,0x24070001);system.cpu().restore(s);check(system.cpu().step(memory).retired,"cached guest fetch");
    memory.write(0x2000,4,0x24070002);s=system.cpu().state();s.pc=0x80002000;s.next_pc=s.pc+4;system.cpu().restore(s);
    check(system.cpu().step(memory).retired && system.cpu().state().gpr[7].low==1,"cached guest code stays stale");
    s=system.cpu().state();s.pc=0xa0001000;s.next_pc=s.pc+4;s.gpr[2].low=0x80002000;
    memory.write(0x1000,4,0xbc4b0000);system.cpu().restore(s);check(system.cpu().step(memory).retired,"guest IHIN");
    s=system.cpu().state();s.pc=0x80002000;s.next_pc=s.pc+4;system.cpu().restore(s);
    check(system.cpu().step(memory).retired && system.cpu().state().gpr[7].low==2,"guest code refresh");
    system.cpu().reset_boot_vector();check(system.cpu().state().cache==CacheState{},"reset invalidates caches");
}
}
int main(){try{behavior();maintenance();controls();guest();std::cout<<"Cache visibility and maintenance tests passed\n";}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
