#include "critterlink/system.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void fault(F fn, MemoryError reason, Access access, std::uint32_t address) {
    try { fn(); } catch (const MemoryFault& error) {
        check(error.reason == reason && error.access == access && error.address == address, "fault metadata");
        return;
    }
    throw std::runtime_error("missing memory fault");
}
template<class F> void rejected(F fn) {
    try { fn(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("missing invalid-argument rejection");
}
void mapping() {
    Memory memory;
    fault([&]{ memory.read(0xbfc00000,4,Access::fetch); },MemoryError::unmapped,Access::fetch,0xbfc00000);
    std::vector<std::uint8_t> rom(Memory::boot_rom_max_size);
    for (std::size_t n=0;n<rom.size();++n) rom[n]=static_cast<std::uint8_t>(n);
    memory.load_boot_rom(rom);
    rom[0]=0xff; // Caller owns its buffer; loading copied it.
    for (auto base : {0x1fc00000U,0x9fc00000U,0xbfc00000U}) {
        check(memory.read(base,1)==0 && memory.read(base,2)==0x0100 &&
              memory.read(base,4,Access::fetch)==0x03020100 &&
              memory.read(base,8)==0x0706050403020100ULL,"ROM little-endian widths and aliases");
        check(memory.read_quadword(base)==std::array<std::uint64_t,2>{0x0706050403020100ULL,0x0f0e0d0c0b0a0908ULL},"ROM quadword");
        check(memory.read_partial(base+1,7)==0x07060504030201ULL,"ROM partial load");
        check(memory.read(base+0x3ffffc,4)==0xfffefdfcU,"ROM final word");
        fault([&]{memory.read_partial(base+0x3fffff,2);},MemoryError::unmapped,Access::load,base+0x3fffff);
        for (unsigned width : {1U,2U,4U,8U}) {
            fault([&]{memory.write(base,width,0);},MemoryError::device,Access::store,base);
        }
        fault([&]{memory.write_partial(base+1,7,0);},MemoryError::device,Access::store,base+1);
        fault([&]{memory.write_quadword(base,{0,0});},MemoryError::device,Access::store,base);
        fault([&]{memory.read(base+1,4);},MemoryError::alignment,Access::load,base+1);
        fault([&]{memory.write(base+1,4,0);},MemoryError::alignment,Access::store,base+1);
    }
    memory.write(0,4,123); memory.clear();
    check(memory.read(0,4)==0 && memory.read(0xbfc00000,4)==0x03020100,"RAM clear preserves ROM");
    const auto before=memory.state();
    rejected([&]{memory.load_boot_rom({});});
    rom.resize(Memory::boot_rom_max_size+1);
    rejected([&]{memory.load_boot_rom(rom);});
    check(memory.state()==before,"invalid loads are atomic");
    auto bad=before;bad.boot_rom.resize(Memory::boot_rom_max_size+1);
    rejected([&]{memory.restore(bad);});check(memory.state()==before,"invalid ROM snapshot is atomic");
    memory.load_boot_rom(memory.boot_rom_bytes().subspan(1,17));
    check(memory.boot_rom_bytes().size()==17 && memory.read(0xbfc00000,4)==0x04030201,"aliased ROM reload");
    fault([&]{memory.read(0xbfc00010,2);},MemoryError::unmapped,Access::load,0xbfc00010);
    memory.restore(before);check(memory.state()==before,"ROM bytes restored with memory");
    auto absent=before;absent.boot_rom.clear();memory.restore(absent);
    fault([&]{memory.read(0xbfc00000,4);},MemoryError::unmapped,Access::load,0xbfc00000);
}
void guest() {
    // Original reset-vector program and BEV syscall handler; no firmware bytes.
    std::vector<std::uint8_t> rom(0x38c);
    auto word=[&](unsigned at,std::uint32_t value){for(unsigned n=0;n<4;++n)rom[at+n]=static_cast<std::uint8_t>(value>>(8*n));};
    word(0,0x40026000); word(4,0x3c01a000); word(8,0xac220100);
    word(12,0x2403002a); word(16,0xac230104); word(20,0x0000000c);
    word(0x380,0x40046800); word(0x384,0xac240108); word(0x388,0x0000080f);
    System system;system.memory().load_boot_rom(rom);system.cpu().reset_boot_vector();
    check(system.cpu().state().pc==0xbfc00000 && system.cpu().state().next_pc==0xbfc00004 &&
          system.cpu().state().cop0.status==0x00400004,"boot vector/reset bits");
    check(system.run(4)==RunResult{4,true},"ROM guest prefix");
    const auto saved=system.state();std::vector<InstructionTrace> first,second;
    check(system.run(5,&first)==RunResult{3,false},"ROM guest exception/stop counts");
    const auto end=system.state();
    check(system.memory().read(0x100,4)==0x00400004 && system.memory().read(0x104,4)==42 &&
          system.memory().read(0x108,4)==0x20 && end.cpu.pc==0xbfc00388 &&
          end.cpu.cop0.epc==0xbfc00014 && end.cpu.cop0.status==0x00400006 &&
          system.memory().hardware().now()==8,"ROM guest literal RAM and exception results");
    check(first.size()==5 && first[1].exception==8U && first.back().stop &&
          first.back().stop->kind==StopKind::unsupported_instruction,"ROM trace");
    system.memory().load_boot_rom(std::array<std::uint8_t,4>{});
    system.restore(saved);system.run(2,&second);system.run(3,&second);
    check(system.state()==end && first==second,"ROM full System replay restores image");
    system.cpu().reset();check(system.cpu().state()==CpuState{},"diagnostic reset stays available");
    System missing;missing.cpu().reset_boot_vector();
    check(missing.run(1)==RunResult{0,false} && missing.memory().hardware().now()==0 &&
          missing.cpu().state().stop->kind==StopKind::unsupported_access,"missing ROM stops without clock progress");
}
}
int main() {
    try { mapping();guest();std::cout<<"Boot ROM mapping and guest tests passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
