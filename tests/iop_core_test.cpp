#include "critterlink/system.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
constexpr std::uint32_t special(unsigned fn,unsigned rs=1,unsigned rt=2,unsigned rd=3,unsigned shift=0) {
    return (rs<<21U)|(rt<<16U)|(rd<<11U)|(shift<<6U)|fn;
}
constexpr std::uint32_t imm(unsigned op,unsigned rs=1,unsigned rt=3,unsigned value=0) {
    return (op<<26U)|(rs<<21U)|(rt<<16U)|value;
}
constexpr std::uint32_t cop(unsigned mode,unsigned rt,unsigned rd) {return 0x40000000U|(mode<<21U)|(rt<<16U)|(rd<<11U);}
void prepare(Iop& iop,std::uint32_t instruction,std::uint32_t a=0,std::uint32_t b=0) {
    iop.start();iop.write32(0,instruction);auto s=iop.state();s.architectural_exceptions=true;s.gpr[1]=a;s.gpr[2]=b;iop.restore(s);
}
void arithmetic() {
    Iop iop;
    const std::array<std::array<std::uint32_t,4>,12> cases{{
        {33,0xffffffff,1,0},{35,0,1,0xffffffff},{32,0x80000000,0x7fffffff,0xffffffff},
        {34,0x80000000,0x80000000,0},{36,0x55aa33cc,0xffff0000,0x55aa0000},
        {37,0x55aa33cc,0x00ff00ff,0x55ff33ff},{38,0x55aa33cc,0x00ff00ff,0x55553333},
        {39,0,0,0xffffffff},{42,0x80000000,1,1},{42,1,0x80000000,0},
        {43,0x80000000,1,0},{43,1,0x80000000,1}}};
    for(const auto& v:cases) {prepare(iop,special(v[0]),v[1],v[2]);check(iop.step() && iop.state().gpr[3]==v[3],"IOP ALU literal oracle");}
    for(unsigned shift=0;shift<32;++shift) {
        prepare(iop,special(2,0,2,3,shift),0,0x80000000);check(iop.step() && iop.state().gpr[3]==(1U<<(31U-shift)),"SRL one-bit sweep");
        prepare(iop,special(7,1,2,3),shift+32,0x80000000);check(iop.step() && iop.state().gpr[3]==static_cast<std::uint32_t>(-2147483648LL/(1LL<<shift)),"SRAV signed power-of-two quotient");
        prepare(iop,special(4),shift+32,1);check(iop.step() && iop.state().gpr[3]==(1U<<shift),"SLLV masks count");
        prepare(iop,special(6),shift+32,0x80000000);check(iop.step() && iop.state().gpr[3]==(1U<<(31U-shift)),"SRLV masks count");
        prepare(iop,special(3,0,2,3,shift),0,0xffffffff);check(iop.step() && iop.state().gpr[3]==0xffffffff,"SRA replicates sign bits");
    }
    for(const auto& v:std::array<std::array<std::uint32_t,4>,5>{{{8,7,0xfffe,5},{10,0xffffffff,0,1},{11,0xffffffff,0xffff,0},{11,0,0xffff,1},{14,0x12345678,0xffff,0x1234a987}}}) {
        prepare(iop,imm(v[0],1,3,v[2]),v[1]);check(iop.step() && iop.state().gpr[3]==v[3],"IOP immediate literal oracle");
    }
    for(unsigned fn:{24U,25U,26U,27U}) {
        prepare(iop,special(fn,1,2,0),0xfffffff9,3);check(iop.step(),"HI/LO operation");
        check(iop.state().hi==(fn==24?0xffffffffU:fn==25?2U:fn==26?0xffffffffU:0U) &&
              iop.state().lo==(fn<26?0xffffffebU:fn==26?0xfffffffeU:0x55555553U),"signed/unsigned multiply/divide literals");
    }
    prepare(iop,special(26,1,2,0),0x80000000,0xffffffff);check(iop.step() && iop.state().lo==0x80000000 && iop.state().hi==0,"signed divide overflow deterministic result");
    for(const auto& v:std::array<std::array<std::uint32_t,4>,4>{{{7,0xfffffffd,0xfffffffe,1},{0xfffffff9,0xfffffffd,2,0xffffffff},{0x80000000,0x80000000,1,0},{0x7fffffff,1,0x7fffffff,0}}}) {
        prepare(iop,special(26,1,2,0),v[0],v[1]);check(iop.step() && iop.state().lo==v[2] && iop.state().hi==v[3],"DIV signs and extrema");
    }
    prepare(iop,special(24,1,2,0),0x80000000,0x80000000);check(iop.step() && iop.state().hi==0x40000000 && iop.state().lo==0,"MULT signed minimum squared");
    prepare(iop,special(25,1,2,0),0xffffffff,0xffffffff);check(iop.step() && iop.state().hi==0xfffffffe && iop.state().lo==1,"MULTU maximum squared");
    prepare(iop,special(17,1,0,0),0x87654321);check(iop.step(),"MTHI");iop.write32(4,special(16,0,0,4));
    iop.write32(8,0);iop.write32(12,0);iop.write32(16,special(19,1,0,0));iop.write32(20,special(18,0,0,5));
    for(unsigned n=0;n<5;++n)check(iop.step(),"spaced HI/LO transfers retire");
    check(iop.state().gpr[4]==0x87654321 && iop.state().gpr[5]==0x87654321,"HI/LO transfers");
    prepare(iop,special(26,1,2,0),1,0);const auto saved=iop.state();check(!iop.step() && iop.state().stop && iop.state().hi==saved.hi && iop.state().lo==saved.lo,"unverified zero divisor stops atomically");
}
void branches() {
    Iop iop;
    for(unsigned kind:{0U,1U,16U,17U}) for(auto value:{0U,1U,0xffffffffU,0x80000000U}) {
        prepare(iop,imm(1,1,kind,2),value);iop.write32(4,imm(9,0,4,9));
        const bool negative=(value&0x80000000U)!=0,taken=(kind&1U)!=0?!negative:negative;
        check(iop.step() && iop.state().delay_slot && iop.state().gpr[31]==(kind>=16?8U:0U),"REGIMM unconditional link");
        const auto saved=iop.state();check(iop.step() && iop.state().pc==(taken?12U:8U) && iop.state().gpr[4]==9,"REGIMM slot and target");
        const auto end=iop.state();iop.restore(saved);check(iop.step() && iop.state()==end,"branch/link replay");
    }
    for(unsigned major:{6U,7U}) for(auto value:{0U,1U,0xffffffffU}) {
        prepare(iop,imm(major,1,0,2),value);iop.write32(4,0);const bool positive=value==1;
        check(iop.step() && iop.step() && iop.state().pc==((major==7?positive:!positive)?12U:8U),"BLEZ/BGTZ boundaries");
    }
    prepare(iop,0x0c000008);iop.write32(4,0);check(iop.step() && iop.state().gpr[31]==8 && iop.step() && iop.state().pc==32,"JAL link/target");
    prepare(iop,special(9,1,0,5),0x20);iop.write32(4,0);check(iop.step() && iop.state().gpr[5]==8 && iop.step() && iop.state().pc==32,"JALR explicit link destination");
}
void merges() {
    Iop iop;
    const std::array<std::uint32_t,4> lwl{0x11b2c3d4,0x2211c3d4,0x332211d4,0x44332211},lwr{0x44332211,0xa1443322,0xa1b24433,0xa1b2c344};
    const std::array<std::uint32_t,4> swl{0xcccccca1,0xcccca1b2,0xcca1b2c3,0xa1b2c3d4},swr{0xa1b2c3d4,0xb2c3d4cc,0xc3d4cccc,0xd4cccccc};
    for(unsigned major:{34U,38U,42U,46U})for(unsigned byte=0;byte<4;++byte) {
        prepare(iop,imm(major,1,2,byte),0x100,0xa1b2c3d4);iop.write32(0x100,major<40?0x44332211:0xcccccccc);iop.write32(4,0);
        check(iop.step(),"merge execution");
        if(major<40)check(iop.state().gpr[2]==0xa1b2c3d4 && iop.step() && iop.state().gpr[2]==(major==34?lwl[byte]:lwr[byte]),"merge delayed literal result");
        else check(iop.read32(0x100)==(major==42?swl[byte]:swr[byte]),"merge selected byte stores");
    }
    for(bool reverse:{false,true}) {
        prepare(iop,imm(reverse?38U:34U,1,2,reverse?1U:4U),0x100,0xa1b2c3d4);
        iop.write32(0x100,0x44332211);iop.write32(0x104,0x88776655);
        iop.write32(4,imm(reverse?34U:38U,1,2,reverse?4U:1U));iop.write32(8,0);
        check(iop.step() && iop.step(),"consecutive merge pair forwards pending value");const auto saved=iop.state();
        check(iop.step() && iop.state().gpr[2]==0x55443322,"both merge pair orders");const auto end=iop.state();
        iop.restore(saved);check(iop.step() && iop.state()==end,"in-flight merged load replay");
    }
}
void exceptions() {
    Iop iop;
    prepare(iop,special(32,1,2,0),0x7fffffff,1);check(!iop.step() && !iop.state().stop && iop.state().cop0.cause==48 && iop.state().gpr[0]==0,"overflow still traps with r0 destination");
    for(const auto& v:std::array<std::array<std::uint32_t,4>,6>{{
        {special(32),0x7fffffff,1,12},{special(34),0x80000000,1,12},{imm(8,1,3,1),0x7fffffff,0,12},
        {12,0,0,8},{13,0,0,9},{imm(35,1,2,1),0x100,0,4}}}) {
        prepare(iop,v[0],v[1],v[2]);auto s=iop.state();s.cop0.status=0x15;s.pending_load=IopLoad{5,0x12345678};iop.restore(s);
        check(!iop.step() && !iop.state().stop && iop.state().pc==0x80000080 && iop.state().cop0.epc==0 &&
              iop.state().cop0.cause==(v[3]<<2U) && iop.state().cop0.status==0x14 && iop.state().gpr[5]==0x12345678 &&
              !iop.state().pending_load && iop.state().gpr[3]==0,"precise exception and older delayed load completion");
    }
    for(auto branch:{0x10000002U,0x14000002U}) {
        prepare(iop,branch);iop.write32(4,12);auto s=iop.state();s.cop0.status=0x400000;iop.restore(s);
        check(iop.step() && !iop.step() && iop.state().pc==0xbfc00180 && iop.state().cop0.epc==0 && iop.state().cop0.cause==0x80000020,"taken/untaken branch slot exception");
    }
    prepare(iop,special(8,1,0,0),0x102);iop.write32(4,imm(9,0,2,9));
    check(iop.step() && iop.step() && iop.state().gpr[2]==9 && !iop.step() && iop.state().cop0.epc==0x102 &&
          iop.state().cop0.bad_vaddr==0x102 && iop.state().cop0.cause==16,"JR address fault occurs after delay slot");
    prepare(iop,imm(43,1,2,1),0x100,0x12345678);iop.write32(0x100,0xaabbccdd);
    check(!iop.step() && iop.state().cop0.cause==20 && iop.state().cop0.bad_vaddr==0x101 && iop.read32(0x100)==0xaabbccdd,"store address fault atomicity");
    prepare(iop,imm(35,1,2),0x80000100);auto s=iop.state();s.cop0.status=2;iop.restore(s);
    check(!iop.step() && iop.state().cop0.cause==16 && iop.state().cop0.bad_vaddr==0x80000100,"user kernel-space address fault");
    prepare(iop,0x44000000);check(!iop.step() && iop.state().cop0.cause==0x1000002c,"COP1 unusable CE");
    prepare(iop,12);auto nested=iop.state();nested.cop0.status=0x15;iop.restore(nested);iop.write32(0x80,13);
    check(!iop.step() && !iop.step() && iop.state().cop0.epc==0x80000080 && iop.state().cop0.status==0x10 &&
          iop.state().cop0.cause==36,"nested exceptions overwrite EPC and push mode stack again");
    const auto before=iop.state();auto invalid=before;invalid.cop0.status|=0x10000;bool rejected=false;
    try{iop.restore(invalid);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected && iop.state()==before,"unsupported cache-isolate snapshot rejects atomically");
}
void cop0() {
    Iop iop;
    for(unsigned reg:{8U,12U,13U,14U}) {
        prepare(iop,cop(0,2,reg));auto s=iop.state();s.cop0={0x400010,0x300,0x87654321,0x12345678};iop.restore(s);iop.write32(4,0);
        const auto expected=reg==8?0x12345678U:reg==12?0x400010U:reg==13?0x300U:0x87654321U;
        check(iop.step() && iop.state().gpr[2]==0 && iop.step() && iop.state().gpr[2]==expected,"MFC0 delayed result");
    }
    prepare(iop,cop(4,1,13),0xffffffff);check(iop.step() && iop.state().cop0.cause==0x300,"Cause writes only software pending bits");
    prepare(iop,0x42000010);auto s=iop.state();s.cop0.status=0x40002c;iop.restore(s);
    check(iop.step() && iop.state().cop0.status==0x40002b && iop.state().pc==4,"RFE restores mode stack without changing PC");
    prepare(iop,cop(0,2,12));s=iop.state();s.cop0.status=2;iop.restore(s);
    check(!iop.step() && iop.state().cop0.cause==44,"user COP0 unusable");
    for(auto status:{0U,1U,0x400U,0x403U,0x401U}) {
        prepare(iop,0);s=iop.state();s.cop0.status=status;iop.restore(s);iop.set_interrupt_line(true);
        const bool delivered=(status&0x401U)==0x401U;
        check(iop.step()!=delivered && iop.state().pc==(delivered?0x80000080U:4U) && (iop.state().cop0.cause&0x400U)!=0,"IOP IP2 mask and IE gate");
        iop.set_interrupt_line(false);check((iop.state().cop0.cause&0x400U)==0,"external line deassertion");
    }
    prepare(iop,imm(9,0,1,1));s=iop.state();s.cop0.status=0x101;s.cop0.cause=0x100;iop.restore(s);
    check(!iop.step() && iop.state().gpr[1]==0 && iop.state().cop0.cause==0x100,"software interrupt before execution");
    iop.reset_boot_vector();check(iop.state().pc==0xbfc00000 && iop.state().cop0.status==0x400000 && iop.state().hi==0 && iop.state().lo==0,"IOP reset vector and controls");
}
void rom_and_replay() {
    System system;std::vector<std::uint8_t> rom(0x204);
    const auto put=[&](unsigned offset,std::uint32_t value){for(unsigned n=0;n<4;++n)rom[offset+n]=static_cast<std::uint8_t>(value>>(n*8));};
    const std::array<std::uint32_t,8> program{0x24010007,0xac010100,12,0x8c020100,0,0xac020104,0x0bf00006,0};
    const std::array<std::uint32_t,7> handler{cop(0,26,14),0,imm(9,26,26,4),cop(4,26,14),0,special(8,26,0,0),0x42000010};
    for(unsigned n=0;n<program.size();++n)put(n*4,program[n]);
    for(unsigned n=0;n<handler.size();++n)put(0x180+n*4,handler[n]);
    system.memory().load_boot_rom(rom);system.memory().iop().reset_boot_vector();system.run(4);
    check(system.memory().iop().state().pending_load && system.memory().iop().state().pc==0xbfc00184,"checkpoint inside ROM exception handler delayed MFC0");
    const auto saved=system.state();std::vector<InstructionTrace>a,b;system.run(16,&a);const auto end=system.state();
    check(!end.memory.hardware.stop && system.memory().iop().read32(0x100)==7 && system.memory().iop().read32(0x104)==7 &&
          system.memory().iop().state().cop0.epc==0xbfc0000c && (system.memory().iop().state().cop0.status&0x3fU)==0,"ROM guest exception return and RAM output");
    system.memory().load_boot_rom(std::array<std::uint8_t,4>{});system.restore(saved);system.run(5,&b);system.run(11,&b);
    check(system.state()==end && a==b,"IOP ROM bytes and in-handler System replay");
    // Both processors consume one shared immutable image, using independent RAM.
    System dual;const std::array<std::uint32_t,4> shared{0x2401002a,0xac010100,0x0bf00002,0};
    for(unsigned n=0;n<shared.size();++n) {put(n*4,shared[n]);}
    dual.memory().load_boot_rom(rom);
    dual.cpu().reset_boot_vector();dual.memory().iop().reset_boot_vector();dual.run(12);
    check(dual.memory().read(0x100,4)==42 && dual.memory().iop().read32(0x100)==42 && !dual.memory().hardware().stop(),"EE and IOP share boot ROM, not RAM");
    System missing;missing.memory().load_boot_rom(std::array<std::uint8_t,3>{});missing.memory().iop().reset_boot_vector();missing.run(1);
    check(missing.memory().iop().state().stop && missing.memory().iop().state().pc==0xbfc00000,"incomplete IOP ROM fetch stops without padding");
    // ROM data accesses use exact byte/halfword/word widths, separately from fetch.
    System widths;const std::array<std::uint32_t,10> reads{0x3c01bfc0,0x90220200,0x94230200,0x8c240200,0,0xac020100,0xac030104,0xac040108,0x0bf00008,0};
    for(unsigned n=0;n<reads.size();++n) {put(n*4,reads[n]);}
    put(0x200,0x89abcdef);
    widths.memory().load_boot_rom(rom);widths.memory().iop().reset_boot_vector();widths.run(10);
    check(!widths.memory().hardware().stop() && widths.memory().iop().read32(0x100)==0xef &&
          widths.memory().iop().read32(0x104)==0xcdef && widths.memory().iop().read32(0x108)==0x89abcdef,"ROM data widths and delayed results");
    widths.memory().iop().write32(0,0xac220000);auto write_rom=widths.memory().iop().state();
    write_rom.pc=0;write_rom.next_pc=4;write_rom.delay_slot=false;write_rom.branch_pc=0;write_rom.pending_load.reset();
    widths.memory().iop().restore(write_rom);widths.run(1);
    check(widths.memory().iop().state().stop && widths.memory().boot_rom_bytes()[0]==rom[0],"IOP ROM store rejects without mutation");
}
}
int main(){try{arithmetic();branches();merges();exceptions();cop0();rom_and_replay();std::cout<<"IOP core and ROM execution tests passed\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
