#include "critterlink/sif.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace critterlink;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Function> void rejects(Function action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("expected rejection");
}
void enable(Sif& sif) {
    sif.iop_write32(0x1f801570, 0x8800);
    sif.iop_write32(0x1f801578, 1);
    sif.iop_write32(0x1f8010f4, 0x800000);
    sif.iop_write32(0x1f801574, 0xc0000);
}
void send_ee(Sif& sif, std::uint32_t count = 8) {
    sif.ee_write32(0x1000c410, 0x100);
    sif.ee_write32(0x1000c420, count);
    sif.ee_write32(0x1000c400, 0x101);
}
void receive_iop(Sif& sif, std::uint32_t blocks = 1) {
    sif.iop_write32(0x1f801530, 0x200);
    sif.iop_write32(0x1f801534, (blocks << 16) | 32);
    sif.iop_write32(0x1f801538, 0x01000200);
}
void send_iop(Sif& sif) {
    sif.iop_write32(0x1f801520, 0x300);
    sif.iop_write32(0x1f801524, 0x10020);
    sif.iop_write32(0x1f801528, 0x01000201);
}
void receive_ee(Sif& sif) {
    sif.ee_write32(0x1000c010, 0x400);
    sif.ee_write32(0x1000c020, 8);
    sif.ee_write32(0x1000c000, 0x100);
}
void messages() {
    Sif sif;
    sif.ee_write32(0x1000f200, 0x12345678);
    sif.iop_write32(0x1d000010, 0x87654321);
    check(sif.iop_read32(0x1d000000) == 0x12345678 &&
          sif.ee_read32(0x1000f210) == 0x87654321, "mailboxes are not shared");
    sif.ee_write32(0x1000f220, 0x10000);
    sif.ee_write32(0x1000f220, 0x20000);
    check(sif.iop_read32(0x1d000020) == 0x30000, "MSFLAG must accumulate");
    sif.iop_write32(0x1d000020, 0x10000);
    check(sif.ee_read32(0x1000f220) == 0x20000, "IOP must acknowledge MSFLAG bits");
    sif.iop_write32(0x1d000030, 3);
    sif.ee_write32(0x1000f230, 1);
    check(sif.iop_read32(0x1d000030) == 2, "SMFLAG acknowledgment");
    const auto before = sif.state();
    rejects([&] { sif.ee_write32(0x1000f210, 5); });
    rejects([&] { sif.iop_write32(0x1d000000, 5); });
    rejects([&] { (void)sif.ee_read32(0x1000f240); });
    check(sif.state() == before, "unsupported mailbox operation changed state");
}
void transfers_and_replay() {
    Sif sif;
    std::vector<std::uint8_t> ee(2048), iop(2048);
    for (std::size_t index = 0; index < 128; ++index) {
        ee[0x100 + index] = static_cast<std::uint8_t>(index ^ 0x5a);
        iop[0x300 + index] = static_cast<std::uint8_t>(255 - index);
    }
    enable(sif);
    send_ee(sif);
    receive_iop(sif);
    send_iop(sif);
    receive_ee(sif);
    sif.tick(ee, iop, true);
    check(sif.state().to_ee.size() == 1 && sif.state().to_iop.size() == 1,
          "both directions must retain in-flight FIFO data");
    check(iop[0x200] == 0 && ee[0x400] == 0, "new data consumed on producing tick");
    sif.tick(ee, iop, true);
    check(sif.state().iop_receive.block_words == 4 && sif.state().ee_receive.qwords == 7,
          "partial transfer state");
    const auto saved = sif.state();
    const auto saved_ee = ee, saved_iop = iop;
    for (int tick = 0; tick < 12; ++tick) sif.tick(ee, iop, true);
    const auto expected = sif.state();
    check(std::equal(ee.begin() + 0x100, ee.begin() + 0x180, iop.begin() + 0x200),
          "EE to IOP bytes differ");
    check(std::equal(iop.begin() + 0x300, iop.begin() + 0x380, ee.begin() + 0x400),
          "IOP to EE bytes differ");
    check(sif.iop_read32(0x1f801574) == 0x0c0c0000 && sif.iop_irq(),
          "IOP DMA completion masks/status");
    check((sif.iop_read32(0x1f8010f4) & 0x80000000) != 0,
          "IOP master interrupt status");
    check((sif.ee_read32(0x1000c000) & 0x100) == 0 &&
          (sif.iop_read32(0x1f801538) & 0x01000000) == 0,
          "completed DMA remains active");
    const auto expected_ee = ee, expected_iop = iop;
    sif.restore(saved);
    ee = saved_ee;
    iop = saved_iop;
    for (int tick = 0; tick < 12; ++tick) sif.tick(ee, iop, true);
    check(sif.state() == expected && ee == expected_ee && iop == expected_iop,
          "in-flight SIF snapshot replay differs");
    check(sif.take_ee_completions() == 0x60 && sif.take_ee_completions() == 0,
          "EE completion events must be consumed exactly once");
    sif.iop_write32(0x1f801574, 0);
    check(sif.iop_read32(0x1f801574) == 0x0c000000 && !sif.iop_irq(),
          "masking pending completions must preserve flags but deassert diagnostic IRQ");
    sif.iop_write32(0x1f801574, 0xc0000);
    check(sif.iop_irq(), "reenabling pending completions must assert diagnostic IRQ");
    sif.iop_write32(0x1f801574, 0x040c0000);
    check(sif.iop_read32(0x1f801574) == 0x080c0000 && sif.iop_irq(), "DICR2 W1C");
    sif.iop_write32(0x1f8010f4, 0);
    check(!sif.iop_irq(), "DICR master mask");
    sif.iop_write32(0x1f801574, 0x080c0000);
    check(sif.iop_read32(0x1f801574) == 0x000c0000, "DICR2 last acknowledgment");
}
void waits_and_backpressure() {
    Sif sif;
    std::vector<std::uint8_t> ee(2048, 17), iop(2048);
    send_ee(sif, 16);
    const auto configured = sif.state();
    sif.tick(ee, iop, false);
    check(sif.state() == configured, "disabled EE DMA advanced");
    for (int tick = 0; tick < 20; ++tick) sif.tick(ee, iop, true);
    check(sif.state().to_iop.size() == Sif::fifo_capacity &&
          sif.state().ee_send.qwords == 8, "FIFO did not bound producer");
    const auto full = sif.state();
    sif.tick(ee, iop, true);
    check(sif.state() == full, "full FIFO changed stalled producer state");
    receive_iop(sif, 2);
    const auto waiting_enable = sif.state();
    sif.tick(ee, iop, true);
    check(sif.state() == waiting_enable, "IOP ignored disabled DMAC");
    enable(sif);
    for (int tick = 0; tick < 16; ++tick) sif.tick(ee, iop, true);
    check(sif.state().to_iop.empty() && sif.state().iop_receive.blocks == 32 &&
          sif.state().ee_send.qwords == 0, "backpressure did not resume");
    check(std::all_of(iop.begin() + 0x200, iop.begin() + 0x300,
                      [](auto byte) { return byte == 17; }), "resumed data corrupt");
    receive_ee(sif);
    const auto empty = sif.state();
    sif.tick(ee, iop, true);
    check(sif.state() == empty, "empty receive FIFO must wait");
    Sif zero;
    zero.ee_write32(0x1000c400, 0x101);
    zero.tick(ee, iop, true);
    check(zero.take_ee_completions() == 0x40 && zero.state().to_iop.empty(),
          "zero QWC completion");
}
void failures() {
    Sif sif;
    const auto before = sif.state();
    rejects([&] { sif.ee_write32(0x1000c400, 0x105); });
    rejects([&] { sif.ee_write32(0x1000c410, 17); });
    rejects([&] { sif.ee_write32(0x1000c420, 65536); });
    rejects([&] { sif.iop_write32(0x1f801528, 0x01000701); });
    rejects([&] { sif.iop_write32(0x1f801524, 0x10004); });
    rejects([&] { sif.iop_write32(0x1f801528, 0x01000201); });
    rejects([&] { sif.iop_write32(0x1f801570, 1); });
    check(sif.state() == before, "unsupported mode mutated registers");
    auto bad = before;
    bad.to_iop.resize(9);
    rejects([&] { sif.restore(bad); });
    bad = before;
    bad.iop_send.chcr = 0x01000201;
    rejects([&] { sif.restore(bad); });
    bad = before;
    bad.iop_send.block_words = 3;
    rejects([&] { sif.restore(bad); });
    bad = before;
    bad.iop_send.blocks = 0x10020;
    bad.iop_send.block_words = 4;
    rejects([&] { sif.restore(bad); });
    check(sif.state() == before, "invalid snapshot mutated state");
    std::vector<std::uint8_t> ee(0x108, 42), iop(1024, 99);
    send_ee(sif);
    const auto active = sif.state();
    rejects([&] { sif.ee_write32(0x1000c410, 0); });
    check(sif.state() == active, "active DMA write mutated state");
    sif.tick(ee, iop, true);
    check(sif.state().stop.has_value() && sif.state().to_iop.empty() &&
          sif.state().ee_send.qwords == 8, "out-of-bounds source consumed data");
    const auto stopped = sif.state();
    sif.tick(ee, iop, true);
    check(sif.state() == stopped, "stopped SIF progressed");
    Sif destination;
    enable(destination);
    send_ee(destination);
    receive_iop(destination);
    ee.resize(1024);
    iop.resize(0x208);
    destination.tick(ee, iop, true);
    destination.tick(ee, iop, true);
    check(destination.state().stop.has_value() && destination.state().to_iop.size() == 1 &&
          destination.state().iop_receive.address == 0x200, "bad destination consumed FIFO");
}
void cancellation() {
    Sif sif;
    std::vector<std::uint8_t> ee(2048), iop(2048, 73);
    enable(sif);
    send_iop(sif);
    for (int tick = 0; tick < 3; ++tick) sif.tick(ee, iop, true);
    check(sif.state().iop_send.block_words == 12 && sif.state().to_ee.size() == 3,
          "IOP cancellation setup");
    const auto address = sif.state().iop_send.address;
    const auto blocks = sif.state().iop_send.blocks;
    sif.iop_write32(0x1f801528, 0);
    check(sif.state().iop_send.address == address && sif.state().iop_send.blocks == blocks &&
          sif.state().iop_send.block_words == 0 && sif.state().to_ee.size() == 3,
          "IOP cancel must preserve registers and queued bytes");
    const auto cancelled = sif.state();
    sif.restore(cancelled);
    sif.tick(ee, iop, true);
    check(sif.state() == cancelled, "cancelled producer progressed or lost queued data");
    sif.ee_write32(0x1000c010, 0x400);
    sif.ee_write32(0x1000c020, 3);
    sif.ee_write32(0x1000c000, 0x100);
    for (int tick = 0; tick < 3; ++tick) sif.tick(ee, iop, true);
    check(sif.state().to_ee.empty() && ee[0x400] == 73 && ee[0x42f] == 73 && ee[0x430] == 0,
          "cancelled DMA queued prefix unavailable to receiver");
    // Completed channels can be reconfigured before starting and saved in that state.
    sif.ee_write32(0x1000c020, 8);
    sif.restore(sif.state());
    send_ee(sif);
    sif.tick(ee, iop, true);
    sif.ee_write32(0x1000c400, 0);
    const auto ee_cancelled = sif.state();
    check(ee_cancelled.ee_send.qwords == 7 && ee_cancelled.to_iop.size() == 1,
          "EE cancel discarded remaining count or FIFO");
    sif.restore(ee_cancelled);
    sif.ee_write32(0x1000c400, 0x101);
    for (int tick = 0; tick < 7; ++tick) sif.tick(ee, iop, true);
    check(sif.state().ee_send.qwords == 0 && sif.state().to_iop.size() == 8,
          "EE restart duplicated or lost queued qwords");
    sif.ee_write32(0x1000c420, 8);
    sif.restore(sif.state());
    // Complete an IOP send and reconfigure its count while CHCR retains mode bits.
    Sif completed;
    enable(completed);
    send_iop(completed);
    for (int tick = 0; tick < 8; ++tick) completed.tick(ee, iop, true);
    completed.iop_write32(0x1f801524, 0x10020);
    completed.restore(completed.state());
}
}
int main() {
    try {
        messages(); transfers_and_replay(); waits_and_backpressure(); failures(); cancellation();
        std::cout << "SIF tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
