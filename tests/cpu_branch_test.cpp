#include "critterlink/cpu.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
constexpr std::uint32_t branch(unsigned op, unsigned rt, unsigned rs=1, unsigned imm=3) {
    return (op<<26U)|(rs<<21U)|(rt<<16U)|imm;
}
}
int main() {
    using namespace critterlink;
    try {
        Memory memory; Cpu cpu;
        const auto setup=[&](std::uint32_t opcode,std::uint64_t a,std::uint64_t b=0) {
            memory.write(0,4,opcode); memory.write(4,4,0x24030007); memory.write(8,4,0);
            CpuState s; s.gpr[1]={a,0xffff}; s.gpr[2]={b,0xffff}; s.gpr[31]={123,456}; cpu.restore(s);
        };
        for (unsigned rt: {0U,1U,2U,3U,16U,17U,18U,19U}) {
            for (const auto a: {0ULL,1ULL,0x8000000000000000ULL,0xffffffffffffffffULL,0x80000000ULL}) {
                setup(branch(1,rt),a);
                const bool negative=(a>>63U)!=0;
                const bool taken=(rt&1U)? !negative:negative;
                const bool annul=(rt&2U)&&!taken;
                check(cpu.step(memory).retired,"REGIMM branch failed");
                check(cpu.state().pc==(annul?8U:4U) && cpu.state().next_pc==(annul?12U:taken?16U:8U) &&
                    cpu.state().delay_slot==!annul,"REGIMM target/delay mismatch");
                check(cpu.state().gpr[31]==Register128{(rt&16U)?8U:123U,456},"link must write on both paths");
                check(cpu.step(memory).retired && cpu.state().gpr[3].low==(annul?0U:7U),"REGIMM annul effect");
            }
        }
        for (unsigned op: {20U,21U,22U,23U}) {
            for (const auto a: {0ULL,1ULL,0x8000000000000000ULL,0x100000000ULL}) {
                const auto b=1ULL;
                setup(branch(op,op<22?2:0),a,b);
                const bool negative=(a>>63U)!=0;
                const bool taken=op==20?a==b:op==21?a!=b:op==22?(a==0||negative):(a!=0&&!negative);
                check(cpu.step(memory).retired,"likely branch failed");
                check(cpu.state().pc==(taken?4U:8U) && cpu.state().delay_slot==taken,"likely annul PC");
                check(cpu.step(memory).retired && cpu.state().gpr[3].low==(taken?7U:0U),"likely slot execution");
            }
        }
        // An annulled slot is never fetched: unsupported opcode, store, and syscall have no effect.
        for (auto slot: {0xffffffffU,0xac030100U,0x0000000cU}) {
            setup(branch(20,2),0,1); memory.write(4,4,slot); memory.write(0x100,4,0xabc);
            std::vector<InstructionTrace> trace;
            check(cpu.run(memory,2,&trace)==RunResult{2,true} && trace[0].pc==0 && trace[1].pc==8 &&
                !trace[1].delay_slot && memory.read(0x100,4)==0xabc,"annul executed slot or wrong trace");
        }
        for (auto opcode: {branch(1,4),branch(1,16,31),branch(1,17,31),branch(1,18,31),
                           branch(1,19,31),branch(22,2),branch(23,2)}) {
            setup(opcode,0); const auto regs=cpu.state().gpr;
            check(cpu.step(memory).stop.has_value() && cpu.state().gpr==regs,"invalid branch encoding accepted");
        }
        setup(branch(1,17),0); memory.write(4,4,branch(20,2));
        check(cpu.step(memory).retired && cpu.step(memory).stop.has_value(),"nested likely branch accepted");
        setup(branch(1,19),0); memory.write(4,4,0x0000000c);
        check(cpu.step(memory).retired && cpu.step(memory).exception==8U && cpu.state().cop0.epc==0 &&
            (cpu.state().cop0.cause&0x80000000U) && cpu.state().gpr[31].low==8,"linked delay exception context");
        setup(branch(1,1,1,0xffff),0);
        check(cpu.step(memory).retired && cpu.state().next_pc==0,"negative offset target");
        const auto checkpoint=cpu.state();
        std::vector<InstructionTrace> first,second;
        check(cpu.run(memory,7,&first)==RunResult{7,true},"branch baseline");
        const auto expected=cpu.state(); cpu.restore(checkpoint);
        for(unsigned n=0;n<7;++n) check(cpu.run(memory,1,&second)==RunResult{1,true},"branch replay");
        check(cpu.state()==expected && first==second,"mid-branch replay changed state/trace");
        // Kernel PC+8 link is sign-extended to the scalar 64-bit register lane.
        setup(branch(1,17),0); auto high=cpu.state();high.pc=0x80000000;high.next_pc=0x80000004;cpu.restore(high);
        check(cpu.step(memory).retired && cpu.state().gpr[31]==Register128{0xffffffff80000008ULL,456},"kernel link extension");
        std::cout<<"EE branch extension tests passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
