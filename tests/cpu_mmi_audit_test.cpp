#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace critterlink;
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
constexpr std::uint32_t encode(unsigned op,unsigned fn,unsigned sub,unsigned rs=1,unsigned rt=2,unsigned rd=3) {
    return (op<<26U)|(rs<<21U)|(rt<<16U)|(rd<<11U)|(sub<<6U)|fn;
}
// Literal transcription of Sony EE instruction manual v6, pp.400-403.
// Empty entries are reserved, not unimplemented instructions.
using Table=std::array<std::string_view,32>;
constexpr Table mmi0{
    "PADDW","PSUBW","PCGTW","PMAXW","PADDH","PSUBH","PCGTH","PMAXH",
    "PADDB","PSUBB","PCGTB","","","","","",
    "PADDSW","PSUBSW","PEXTLW","PPACW","PADDSH","PSUBSH","PEXTLH","PPACH",
    "PADDSB","PSUBSB","PEXTLB","PPACB","","","PEXT5","PPAC5"};
constexpr Table mmi1{
    "","PABSW","PCEQW","PMINW","PADSBH","PABSH","PCEQH","PMINH",
    "","","PCEQB","","","","","",
    "PADDUW","PSUBUW","PEXTUW","","PADDUH","PSUBUH","PEXTUH","",
    "PADDUB","PSUBUB","PEXTUB","QFSRV","","","",""};
constexpr Table mmi2{
    "PMADDW","","PSLLVW","PSRLVW","PMSUBW","","","",
    "PMFHI","PMFLO","PINTH","","PMULTW","PDIVW","PCPYLD","",
    "PMADDH","PHMADH","PAND","PXOR","PMSUBH","PHMSBH","","",
    "","","PEXEH","PREVH","PMULTH","PDIVBW","PEXEW","PROT3W"};
constexpr Table mmi3{
    "PMADDUW","","","PSRAVW","","","","",
    "PMTHI","PMTLO","PINTEH","","PMULTUW","PDIVUW","PCPYUD","",
    "","","POR","PNOR","","","","",
    "","","PEXCH","PCPYH","","","PEXCW",""};
CpuState initial() {
    CpuState s;s.gpr[1]={7,9};s.gpr[2]={2,3};s.gpr[3]={42,43};s.hi={11,12};s.lo={13,14};s.sa=24;return s;
}
void inventory() {
    Memory memory;Cpu cpu;unsigned accepted=0;
    for(unsigned fn=0;fn<64;++fn) for(unsigned sub=0;sub<32;++sub) {
        unsigned rs=1,rt=2,rd=3;bool valid=false;
        const Table* table=fn==8?&mmi0:fn==40?&mmi1:fn==9?&mmi2:fn==41?&mmi3:nullptr;
        if(table) {
            const auto name=(*table)[sub];valid=!name.empty();
            if(name=="PABSW" || name=="PABSH" || name=="PEXT5" || name=="PPAC5" ||
                name=="PEXEH" || name=="PREVH" || name=="PEXEW" || name=="PROT3W" ||
                name=="PEXCH" || name=="PCPYH" || name=="PEXCW") rs=0;
            if(name=="PMFHI" || name=="PMFLO") {rs=0;rt=0;}
            if(name=="PMTHI" || name=="PMTLO") {rt=0;rd=0;}
            if(name=="PDIVW" || name=="PDIVUW" || name=="PDIVBW") rd=0;
        } else {
            switch(fn) {
            case 0:case 1:case 24:case 25:case 32:case 33:valid=sub==0;break;
            case 4:rt=0;valid=sub==0;break;
            case 16:case 18:rs=0;rt=0;valid=sub==0;break;
            case 17:case 19:rt=0;rd=0;valid=sub==0;break;
            case 26:case 27:rd=0;valid=sub==0;break;
            case 48:rs=0;rt=0;valid=sub<5;break;
            case 49:rt=0;rd=0;valid=sub==0;break;
            case 52:case 60:case 62:case 63:rs=0;valid=true;break;
            case 54:case 55:rs=0;valid=sub<16;break;
            default:break;
            }
        }
        memory.write(0,4,encode(28,fn,sub,rs,rt,rd));auto before=initial();cpu.restore(before);
        const auto trace=cpu.step(memory);
        if(valid) {
            if(!trace.retired || trace.stop || trace.exception) {
                std::cerr<<"Missing canonical MMI fn="<<fn<<" sub="<<sub<<'\n';
                throw std::runtime_error("MMI inventory entry rejected");
            }
            check(cpu.state().pc==4 && cpu.state().next_pc==8 && cpu.state().gpr[0]==Register128{},"MMI retirement invariants");++accepted;
        } else {
            before.stop=cpu.state().stop;
            check(trace.stop && !trace.exception && !trace.retired && cpu.state()==before,"reserved/restricted MMI slot accepted or mutated state");
        }
    }
    check(accepted==257,"manual MMI slot inventory count changed");
}
void overflow() {
    Memory memory;Cpu cpu;
    for(unsigned kind=0;kind<3;++kind) for(unsigned lane=0;lane<(kind==2?3U:1U);++lane) {
        const auto instruction=kind==0?encode(0,26,0,1,2,0):kind==1?encode(28,26,0,1,2,0):encode(28,9,13,1,2,0);
        auto before=initial();
        before.gpr[1]={0xffffffff80000000ULL,0xffffffff80000000ULL};before.gpr[2]={~0ULL,~0ULL};
        auto expected=before;
        if(kind==2) {
            if(lane==0) {before.gpr[1].high=7;before.gpr[2].high=3;expected=before;expected.lo={0xffffffff80000000ULL,2};expected.hi={0,1};}
            else if(lane==1) {before.gpr[1].low=7;before.gpr[2].low=3;expected=before;expected.lo={2,0xffffffff80000000ULL};expected.hi={1,0};}
            else {expected.lo={0xffffffff80000000ULL,0xffffffff80000000ULL};expected.hi={};}
        } else { (kind==0?expected.lo.low:expected.lo.high)=0xffffffff80000000ULL;(kind==0?expected.hi.low:expected.hi.high)=0; }
        memory.write(0,4,instruction);cpu.restore(before);expected.pc=4;expected.next_pc=8;
        const auto trace=cpu.step(memory);check(trace.retired && !trace.stop && !trace.exception && cpu.state()==expected,"signed divide overflow result/preservation");
        for(auto branch:{0x10000003U,0x14000003U,0x54000003U}) {
            memory.write(0,4,branch);memory.write(4,4,instruction);cpu.restore(before);check(cpu.step(memory).retired,"divide edge branch");
            if(branch==0x54000003U) check(cpu.state().pc==8 && cpu.state().hi==before.hi && cpu.state().lo==before.lo,"annulled divide modified state");
            else {
                const auto saved=cpu.state();const auto first=cpu.step(memory);const auto after=cpu.state();cpu.restore(saved);
                check(cpu.step(memory)==first && cpu.state()==after && first.retired && first.delay_slot && after.hi==expected.hi && after.lo==expected.lo &&
                    after.pc==(branch==0x10000003U?16U:8U),"divide overflow delay replay");
            }
        }
    }
}
void guest() {
    System system;auto& memory=system.memory();
    memory.write_quadword(0x2000,{0xffffffff80000000ULL,0xffffffff80000000ULL});memory.write_quadword(0x2010,{~0ULL,~0ULL});
    const std::array program{0x78810000U,0x78820010U,encode(0,26,0,1,2,0),encode(28,26,0,1,2,0),
        encode(28,9,9,0,0,3),0x7c830020U,encode(28,9,13,1,2,0),encode(28,9,9,0,0,5),0x7c850030U};
    for(unsigned n=0;n<program.size();++n)memory.write(n*4U,4,program[n]);
    CpuState start;start.gpr[4].low=0x2000;system.cpu().restore(start);check(system.run(3)==RunResult{3,true},"divide edge guest setup");
    const auto saved=system.state();std::vector<InstructionTrace> first,second;check(system.run(6,&first)==RunResult{6,true},"divide edge guest execution");
    const std::array<std::uint64_t,2> output{0xffffffff80000000ULL,0xffffffff80000000ULL};
    check(memory.read_quadword(0x2020)==output && memory.read_quadword(0x2030)==output && system.cpu().state().hi==Register128{},"divide edge guest RAM");
    const auto expected=system.state();system.restore(saved);system.run(2,&second);system.run(4,&second);
    check(system.state()==expected && first==second,"divide edge full-System replay");
}
}
int main() {
    try {inventory();overflow();guest();std::cout<<"MMI inventory and divide conformance tests passed\n";}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
