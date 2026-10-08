#include "critterlink/system.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace critterlink;
using Qword = std::array<std::uint64_t, 2>;
constexpr std::uint32_t fifo = 0x10006000, ctrl = 0x10003000, gif_stat = 0x10003020;
constexpr Qword empty_tag{0x1000000000008000ULL, 14};
constexpr std::array<Qword, 6> sprite{{
    {0x1000000000008005ULL,14}, {6,0}, {0x10000,0x4c},
    {0x80402010,1}, {0x00300020,5}, {0x00700050,5}
}};
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class Exception, class F> void rejects(F fn) {
    try { fn(); } catch (const Exception&) { return; }
    throw std::runtime_error("missing expected rejection");
}
void fill(Memory& memory) {
    for (unsigned i=0;i<16;++i) memory.write_quadword(fifo+i*16,empty_tag);
}
void dma(Memory& memory, unsigned count) {
    memory.write(0x1000e000,4,1);
    memory.write(0x1000a010,4,0x2000);
    memory.write(0x1000a020,4,count);
    memory.write(0x1000a000,4,0x101);
}
void pixels(const GraphicsState& graphics) {
    for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x)
        check(graphics.pixels[y*64+x] == ((x>=2 && x<5 && y>=3 && y<7)?0x80402010U:0U),
              "guest FIFO sprite pixel mismatch");
}
void fifo_capacity_and_access() {
    Memory memory;
    memory.write(ctrl,4,8); fill(memory);
    const auto full=memory.state();
    check((memory.read(gif_stat,4)&0x1f000008U)==0x10000008U,"FIFO status count/pause");
    rejects<MemoryStall>([&]{ memory.write_quadword(fifo,empty_tag); });
    check(memory.state()==full,"17th write mutated full FIFO");
    memory.advance(3);
    check(memory.hardware().state().gif_fifo==full.hardware.gif_fifo,"pause drained FIFO");
    const auto before_rejections = memory.state();
    for(unsigned width : {1U,2U,4U,8U}) {
        rejects<MemoryFault>([&]{ memory.write(fifo,width,0); });
        rejects<MemoryFault>([&]{ memory.read(fifo,width); });
    }
    rejects<MemoryFault>([&]{ memory.read_quadword(fifo); });
    rejects<MemoryFault>([&]{ memory.write_quadword(0x10007000,empty_tag); });
    rejects<MemoryFault>([&]{ memory.write_quadword(fifo+1,empty_tag); });
    rejects<MemoryFault>([&]{ memory.write_partial(fifo,4,0); });
    rejects<MemoryFault>([&]{ memory.read_partial(fifo,4); });
    rejects<MemoryFault>([&]{ memory.write(ctrl,4,2); });
    check(memory.state()==before_rejections,"rejected FIFO transaction changed state");
    memory.write(ctrl,4,0); memory.advance(16);
    check(memory.hardware().state().gif_fifo.count==0 && !memory.hardware().stop(),"FIFO resume");
    for(auto address : {0x10006ff0U,0x90006000U,0xb0006ff0U}) memory.write_quadword(address,empty_tag);
    check(memory.hardware().state().gif_fifo.count==3,"FIFO address aliases");
    memory.advance(3);

}

void direct_backpressure() {
    Hardware hardware;
    for(unsigned i=0;i<16;++i) check(hardware.write_quadword(fifo,{0x8000,0x10000000,14,0}),"direct enqueue");
    const auto full=hardware.state();
    check(!hardware.write_quadword(fifo,{1,2,3,4}) && hardware.state()==full,"direct backpressure atomicity");

}

void cpu_dma_order() {
    // CPU tag precedes DMA payload in one queue; DMA completion is enqueue completion.
    Memory memory; memory.write(ctrl,4,8);
    memory.write_quadword(fifo,{0x1000000000008001ULL,14});
    memory.write_quadword(0x2000,{0x12345678,1}); dma(memory,1); memory.advance(1);
    auto h=memory.hardware().state();
    check(h.gif_fifo.count==2 && h.dma.qwords==0 && !(h.dma.chcr&0x100U) &&
          h.graphics.rgba!=0x12345678,"DMA enqueue completion/order");
    memory.write(ctrl,4,0); memory.advance(2);
    check(memory.hardware().graphics().rgba==0x12345678,"shared CPU/DMA FIFO order");

}

void dma_retry() {
    Memory memory; memory.write(ctrl,4,8); fill(memory);
    memory.write_quadword(0x2000,empty_tag); dma(memory,1);
    memory.advance(1);
    check(memory.hardware().state().dma.qwords==1 && memory.hardware().state().dma.address==0x2000,"full DMA advanced");
    memory.write(ctrl,4,0); memory.advance(1);
    check(memory.hardware().state().dma.qwords==1 && memory.hardware().state().gif_fifo.count==15,"DMA must wait before same-tick drain");
    memory.advance(1);
    check(memory.hardware().state().dma.qwords==0 && memory.hardware().state().gif_fifo.count==15,"DMA retry did not enqueue once");

}

void guest_sprite() {
    System system; auto& memory=system.memory();
    for(unsigned i=0;i<sprite.size();++i) {
        memory.write_quadword(0x2000+i*16,sprite[i]);
        memory.write(i*8,4,(30U<<26)|(1U<<21)|(2U<<16)|(i*16)); // LQ r2,offset(r1)
        memory.write(i*8+4,4,(31U<<26)|(3U<<21)|(2U<<16)); // SQ r2,0(r3)
    }
    CpuState cpu; cpu.gpr[1].low=0x2000; cpu.gpr[3].low=0xb0006fff; system.cpu().restore(cpu);
    check(system.run(12)==RunResult{12,true},"guest LQ/SQ run"); pixels(memory.hardware().graphics());

}

void cpu_retry() {
    System system; auto& memory=system.memory(); fill(memory);
    memory.write(0,4,(31U<<26)|(1U<<21)|(2U<<16));
    CpuState cpu; cpu.gpr[1].low=fifo; cpu.gpr[2]={empty_tag[0],empty_tag[1]}; system.cpu().restore(cpu);
    const auto before=system.cpu().state();
    const auto first=system.cpu().step(memory);
    check(first.stalled && !first.retired && !first.stop && !first.exception && system.cpu().state()==before,"SQ full changed CPU");
    check(system.cpu().run(memory,2)==RunResult{0,true} && memory.hardware().state().gif_fifo.count==16,"CPU-only run drained FIFO");
    std::vector<InstructionTrace> trace;
    check(system.run(2,&trace)==RunResult{1,true} && trace.size()==2 && trace[0].stalled && trace[1].retired &&
          system.cpu().state().pc==4 && memory.hardware().state().gif_fifo.count==15,"System stall/retry count");

}

void delay_slot_retry() {
    System system; auto& memory=system.memory(); memory.write(ctrl,4,8); fill(memory);
    memory.write(0,4,0x10000003); memory.write(4,4,(31U<<26)|(1U<<21)|(2U<<16));
    CpuState cpu; cpu.gpr[1].low=fifo; cpu.gpr[2]={empty_tag[0],empty_tag[1]}; system.cpu().restore(cpu);
    check(system.run(1)==RunResult{1,true},"delay branch");
    auto expected=system.cpu().state();expected.cop0.count+=2;std::vector<InstructionTrace> trace;
    check(system.run(2,&trace)==RunResult{0,true} && trace[0].stalled && trace[0].delay_slot &&
          system.cpu().state()==expected,"stall lost delay context or timer progress");
    memory.write(ctrl,4,0);
    check(system.run(2)==RunResult{1,true} && system.cpu().state().pc==16 && !system.cpu().state().delay_slot,"delay retry target");

}

void interrupt_retry() {
    // An interrupt can arrive while SQ waits, before any store is accepted.
    System system; auto& memory=system.memory(); fill(memory);
    memory.write(0,4,(31U<<26)|(1U<<21)|(2U<<16));
    memory.write(0x200,4,0xac800010); // SW r0,MODE(r4): disable timer.
    memory.write(0x204,4,0xacc50000); // SW r5,STAT(r6): acknowledge IRQ.
    memory.write(0x208,4,0x42000018); // ERET: retry original SQ.
    CpuState cpu; cpu.gpr[1].low=fifo; cpu.gpr[2]={empty_tag[0],empty_tag[1]};
    cpu.gpr[4].low=0x10000000; cpu.gpr[5].low=0x200; cpu.gpr[6].low=0x1000f000;
    cpu.cop0.status=0x10401; system.cpu().restore(cpu);
    memory.write(0x10000020,4,1); memory.write(0x10000010,4,0x180);
    memory.write(0x1000f010,4,0x200);
    std::vector<InstructionTrace> trace;
    check(system.run(2,&trace)==RunResult{0,true} && trace[0].stalled &&
          trace[1].exception==0U && system.cpu().state().cop0.epc==0 &&
          memory.hardware().state().gif_fifo.count==14,"interrupt did not precede stalled store retry");
    check(system.run(4)==RunResult{4,true} && system.cpu().state().pc==4 &&
          memory.hardware().state().gif_fifo.count==11,"ERET did not retry stalled SQ exactly once");

}

void rejected_packet() {
    Memory memory;
    memory.write_quadword(fifo,{0x1400000000000001ULL,2}); // REGLIST ST descriptor is unsupported, but posted into the queue.
    const auto before=memory.hardware().state();
    memory.advance(1);
    const auto failed=memory.state();
    check(failed.hardware.stop && failed.hardware.gif_fifo==before.gif_fifo &&
          failed.hardware.graphics==before.graphics,"rejected head was consumed or changed decoder");
    memory.advance(2); check(memory.state()==failed,"faulted FIFO kept progressing");
    memory.restore(failed); check(memory.state()==failed,"faulted FIFO snapshot changed");

}

void snapshot_replay() {
    System system; auto& memory=system.memory();
    for(const auto& qword : sprite) memory.write_quadword(fifo,qword);
    check(system.run(4)==RunResult{4,true},"partial queue run");
    const auto checkpoint=system.state();
    check(checkpoint.memory.hardware.gif_fifo.count==2 && checkpoint.memory.hardware.graphics.remaining==2,"checkpoint not partial");
    std::vector<InstructionTrace> first,second; system.run(2,&first);
    const auto expected=system.state(); pixels(memory.hardware().graphics());
    system.restore(checkpoint); system.run(1,&second); system.run(1,&second);
    check(system.state()==expected && first==second,"FIFO snapshot replay");
    for(unsigned kind=0;kind<3;++kind) {
        auto bad=checkpoint;
        if(kind==0) bad.memory.hardware.gif_fifo.head=16;
        if(kind==1) bad.memory.hardware.gif_fifo.count=17;
        if(kind==2) bad.memory.hardware.gif_fifo.words[0]={1,2,3,4};
        rejects<std::invalid_argument>([&]{system.restore(bad);});
        check(system.state()==expected,"invalid FIFO snapshot not atomic");
    }
    system.restore(checkpoint); system.run(1);
    const auto graphics=memory.hardware().graphics();
    check(graphics.vertex_pending && graphics.remaining==1,"reset checkpoint missing vertex");
    memory.write(ctrl,4,1);
    auto reset_graphics=graphics; reset_graphics.remaining=0; reset_graphics.gif_eop=false;
    reset_graphics.regs=0; reset_graphics.nreg=0; reset_graphics.reg_index=0;
    check(memory.hardware().state().gif_fifo.count==0 && memory.hardware().state().gif_owner==0 &&
          memory.hardware().graphics()==reset_graphics,"GIF reset changed GS state");

}

}
int main() {
    try {
        fifo_capacity_and_access();
        direct_backpressure();
        cpu_dma_order();
        dma_retry();
        guest_sprite();
        cpu_retry();
        delay_slot_retry();
        interrupt_retry();
        rejected_packet();
        snapshot_replay();
        std::cout << "GIF FIFO tests passed\n";
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
