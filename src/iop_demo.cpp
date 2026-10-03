#include "critterlink/iop_demo.hpp"

#include <stdexcept>

namespace critterlink {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
constexpr std::uint32_t immediate(unsigned op, unsigned rs, unsigned rt, std::uint16_t value) {
    return (op << 26U) | (rs << 21U) | (rt << 16U) | value;
}
void constant(std::vector<std::uint32_t>& code, unsigned reg, std::uint32_t value) {
    code.push_back(immediate(15, 0, reg, static_cast<std::uint16_t>(value >> 16U)));
    code.push_back(immediate(13, reg, reg, static_cast<std::uint16_t>(value)));
}
void store_constant(std::vector<std::uint32_t>& code, unsigned base, std::uint16_t offset,
                    std::uint32_t value) {
    constant(code, 2, value);
    code.push_back(immediate(43, base, 2, offset));
}
void idle(std::vector<std::uint32_t>& code) {
    code.push_back(0x08000000U | static_cast<std::uint32_t>(code.size()));
    code.push_back(0);
}
}
void prepare_iop_demo(System& system) {
    std::vector<std::uint32_t> ee;
    constant(ee, 1, 0x1000e000);
    store_constant(ee, 1, 0, 1); // DMAE
    constant(ee, 1, 0x1000c000);
    store_constant(ee, 1, 0x10, 0x3000);
    store_constant(ee, 1, 0x20, 8);
    store_constant(ee, 1, 0, 0x100); // EE SIF0 receive
    constant(ee, 1, 0x1000c400);
    store_constant(ee, 1, 0x10, 0x2000);
    store_constant(ee, 1, 0x20, 8);
    store_constant(ee, 1, 0, 0x101); // EE SIF1 send
    constant(ee, 10, 0x1000f200);
    const auto mailbox_poll = static_cast<std::uint32_t>(ee.size());
    ee.push_back(immediate(35, 10, 11, 0x10)); // EE polls IOP-to-EE mailbox
    ee.push_back(0x08000000U | mailbox_poll);
    ee.push_back(0);
    for (std::size_t n = 0; n < ee.size(); ++n)
        system.memory().write(static_cast<std::uint32_t>(n * 4), 4, ee[n]);
    constexpr std::array<std::uint32_t, 4> request{7, 9, 11, 13};
    for (std::uint32_t n = 0; n < 32; ++n)
        system.memory().write(0x2000 + n * 4, 4, n < request.size() ? request[n] : 0);

    // The delayed load deliberately overlaps queued SIF data for snapshot coverage.
    std::vector<std::uint32_t> iop(30, 0);
    iop.push_back(immediate(35, 0, 7, 0x800));
    constant(iop, 1, 0x1f801570);
    store_constant(iop, 1, 0, 0x8800); // Enable IOP channels 9 and 10
    store_constant(iop, 1, 8, 1);      // Global IOP DMA enable
    constant(iop, 1, 0x1f801530);
    store_constant(iop, 1, 0, 0x1000);
    store_constant(iop, 1, 4, 0x00010020); // One 32-word block
    store_constant(iop, 1, 8, 0x01000200);
    iop.push_back(immediate(9, 0, 4, 32));
    const auto poll = iop.size();
    iop.push_back(immediate(35, 1, 3, 4)); // Poll remaining block count
    iop.push_back(0);                     // R3000 load delay
    iop.push_back(immediate(5, 3, 4, 0xfffd)); // BNE -> poll
    iop.push_back(0);
    require(poll + 4 == iop.size(), "invalid IOP polling fixture");
    iop.push_back(immediate(9, 0, 5, 0));
    for (std::uint16_t n = 0; n < 4; ++n) {
        iop.push_back(immediate(35, 0, 6, static_cast<std::uint16_t>(0x1000 + n * 4)));
        iop.push_back(0);
        iop.push_back((5U << 21U) | (6U << 16U) | (5U << 11U) | 0x21U); // ADDU
    }
    iop.push_back(immediate(43, 0, 5, 0x2000));
    constant(iop, 10, 0x1d000000);
    iop.push_back(immediate(43, 10, 5, 0x10)); // Publish result through SMCOM
    constant(iop, 1, 0x1f801520);
    store_constant(iop, 1, 0, 0x2000);
    store_constant(iop, 1, 4, 0x00010020);
    store_constant(iop, 1, 8, 0x01000201);
    idle(iop);
    for (std::size_t n = 0; n < iop.size(); ++n)
        system.memory().iop().write32(static_cast<std::uint32_t>(n * 4), iop[n]);
    system.memory().iop().start();
}
IopDemoResult run_iop_demo() {
    System system;
    prepare_iop_demo(system);
    require(system.run(31) == RunResult{31, true}, "IOP fixture stopped before checkpoint");
    const auto checkpoint = system.state();
    require(checkpoint.memory.hardware.iop.pending_load.has_value() &&
        !checkpoint.memory.hardware.sif.to_iop.empty(), "IOP checkpoint lacks pending load or SIF data");
    std::vector<InstructionTrace> first, second;
    require(system.run(192, &first) == RunResult{192, true}, "IOP exchange stopped");
    const auto expected = system.state();
    system.restore(checkpoint);
    for (unsigned n = 0; n < 192; ++n)
        require(system.run(1, &second) == RunResult{1, true}, "IOP replay stopped");
    const auto& hardware = system.memory().hardware();
    const auto& sif = hardware.sif();
    const auto response = static_cast<std::uint32_t>(system.memory().read(0x3000, 4));
    require(response == 40 && sif.smcom == 40 && system.cpu().state().gpr[11].low == 40,
        "IOP DMA/mailbox response differs from independently expected sum");
    for (std::uint32_t n = 1; n < 32; ++n)
        require(system.memory().read(0x3000 + n * 4, 4) == 0, "IOP response padding changed");
    const bool complete = sif.ee_send.qwords == 0 && sif.ee_receive.qwords == 0 &&
        (sif.iop_send.chcr & 0x01000000U) == 0 &&
        (sif.iop_receive.chcr & 0x01000000U) == 0 &&
        sif.to_ee.empty() && sif.to_iop.empty() &&
        (hardware.read(0x1000e010) & 0x60U) == 0x60U;
    return {hardware.now(), response, complete, system.state() == expected && first == second};
}
} // namespace critterlink
