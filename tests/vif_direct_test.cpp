#include "critterlink/system.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace critterlink;
using Qword = std::array<std::uint64_t, 2>;
constexpr std::uint32_t vifstat = 0x10003c00;
constexpr std::uint32_t chcr = 0x10009000, madr = 0x10009010, qwc = 0x10009020;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class T> auto snap(const T& object) {
    using S = std::remove_cvref_t<decltype(object.state())>;
    return std::unique_ptr<S>(new S(object.state()));
}
template<class E = MemoryFault, class F> void rejects(F fn) {
    try { fn(); }
    catch (const E&) { return; }
    throw std::runtime_error("missing rejection");
}
Qword pixels(unsigned first) {
    return {first | (std::uint64_t{first + 1} << 32),
            (first + 2) | (std::uint64_t{first + 3} << 32)};
}
void start(Memory& m, unsigned count, unsigned address = 0x3000) {
    m.write(0x1000e000, 4, 1);
    m.write(madr, 4, address);
    m.write(qwc, 4, count);
    m.write(chcr, 4, 0x181);
}
void single(Memory& m, std::uint32_t command) {
    m.write_quadword(0x2800, {0, std::uint64_t{command} << 32});
    start(m, 1, 0x2800);
    m.advance(1);
}
void direct(Memory& m, const std::vector<Qword>& packet, unsigned cmd = 0x50) {
    m.write_quadword(0x3000, {0, std::uint64_t{(cmd << 24) | 0xab0000U |
                         static_cast<unsigned>(packet.size())} << 32});
    for (unsigned n = 0; n < packet.size(); ++n) m.write_quadword(0x3010 + n * 16, packet[n]);
    start(m, static_cast<unsigned>(packet.size()) + 1);
}

void literal_path2_pixels() {
    for (unsigned cmd : {0x50U, 0x51U}) {
        auto m = std::make_unique<Memory>();
        direct(*m, {{0x1000000000008005ULL, 14}, {6, 0}, {0x10000, 0x4c},
                    {0x80500000, 1}, {0, 5}, {0x00100020, 5}}, cmd);
        m->advance(9);
        check(!m->hardware().stop() && m->hardware().graphics().pixels[0] == 0x80500000 &&
              m->hardware().graphics().pixels[1] == 0x80500000 &&
              m->hardware().graphics().pixels[2] == 0, "DIRECT literal sprite/data command confusion");
        check(m->read(qwc, 4) == 0, "DIRECT count did not finish exact payload");
    }
}

void alignment_counts_and_tte() {
    for (unsigned lane = 0; lane < 3; ++lane) {
        auto m = std::make_unique<Memory>();
        std::array<std::uint32_t, 4> words{};
        words[lane] = 0x50000001;
        m->write_quadword(0x3000, {words[0] | (std::uint64_t{words[1]} << 32),
                                  words[2] | (std::uint64_t{words[3]} << 32)});
        start(*m, 1);
        m->advance(1);
        check(m->hardware().stop() && snap(m->hardware())->vif_transport.direct_remaining == 0,
              "misaligned DIRECT command consumed");
    }
    auto zero = std::make_unique<Memory>();
    single(*zero, 0x50ff0000);
    check(!zero->hardware().stop() && snap(zero->hardware())->vif_transport.direct_remaining == 65536,
          "DIRECT IMM0 is not65536");
    rejects([&] { zero->write(0x12001040, 8, 1); });
    auto interrupt = std::make_unique<Memory>();
    single(*interrupt, 0xd0000001);
    check(interrupt->hardware().stop() && snap(interrupt->hardware())->vif_transport.direct_remaining == 0,
          "unsupported DIRECT I bit accepted");
    auto tte = std::make_unique<Memory>();
    tte->write_quadword(0x2000, {0x70000001, 0x5000000100000000ULL});
    tte->write_quadword(0x2010, {0x1000000000008000ULL, 14});
    tte->write(0x1000e000, 4, 1);
    tte->write(0x10009030, 4, 0x2000);
    tte->write(chcr, 4, 0x1c5);
    tte->advance(4);
    check(!tte->hardware().stop() && tte->read(qwc, 4) == 0,
          "TTE tagword3 DIRECT alignment rejected");
    auto inserted = std::make_unique<Memory>();
    inserted->write_quadword(0x2000, {0x10000000, 0x5000000100000000ULL});
    inserted->write_quadword(0x2010, {0x70000001, 0});
    inserted->write_quadword(0x2020, {0, 0});
    inserted->write(0x1000e000, 4, 1);
    inserted->write(0x10009030, 4, 0x2000);
    inserted->write(chcr, 4, 0x1c5);
    inserted->advance(4);
    check(inserted->hardware().stop() && snap(inserted->hardware())->vif_transport.lane == 0,
          "active DIRECT consumed inserted TTE half-qword");
}

void ownership_and_mask() {
    auto m = std::make_unique<Memory>();
    // PATH3 begins a two-tag packet; the first completed tag has EOP0.
    m->write_quadword(0x10006000, {0x1000000000000001ULL, 14});
    m->write_quadword(0x10006000, {0x11111111, 1});
    m->advance(2);
    check(snap(m->hardware())->gif_owner == 3, "EOP0 lost PATH3 packet ownership");
    direct(*m, {{0x1000000000008001ULL, 14}, {0x33333333, 1}});
    m->advance(3);
    check(m->hardware().graphics().rgba == 0x11111111 &&
          snap(m->hardware())->gif_path2_fifo.count == 2, "PATH2 switched inside EOP0 packet");
    single(*m, 0x06ff8001); // Ignored NUM/IMM bits; bit15 masks future PATH3 grants.
    m->write_quadword(0x10006000, {0x1000000000008001ULL, 14});
    m->write_quadword(0x10006000, {0x22222222, 1});
    m->advance(4);
    check(m->hardware().graphics().rgba == 0x33333333,
          "mask stopped active PATH3 packet or PATH2 priority boundary failed");
    m->write_quadword(0x10006000, {0x1000000000008001ULL, 14});
    m->write_quadword(0x10006000, {0x44444444, 1});
    m->advance(3);
    check(snap(m->hardware())->gif_fifo.count == 2 &&
          m->hardware().graphics().rgba == 0x33333333, "masked PATH3 dropped or consumed queued data");
    single(*m, 0x06017fff);
    m->advance(2);
    check(m->hardware().graphics().rgba == 0x44444444 && !m->hardware().stop(),
          "unmask did not preserve queued PATH3 order");
}

void assembler_backpressure_and_flush() {
    auto m = std::make_unique<Memory>();
    m->write(0x10003000, 4, 8);
    std::vector<Qword> payload(17, {0x1000000000008000ULL, 14});
    direct(*m, payload);
    m->advance(18);
    const auto stalled = snap(*m);
    check(stalled->hardware.gif_path2_fifo.count == 16 &&
          stalled->hardware.vif_transport.lane == 3 &&
          stalled->hardware.vif_dma.cursor == 3 && m->read(qwc, 4) == 1,
          "full PATH2 queue did not preserve fourth assembler word");
    rejects([&] { m->write(0x12001040, 8, 1); });
    m->write(0x1000e000, 4, 0);
    m->write(0x10003000, 4, 0);
    m->advance(4);
    check(m->read(qwc, 4) == 1, "DMAE pause consumed pending DIRECT word");
    m->write(0x1000e000, 4, 1);
    m->advance(20);
    const auto done = snap(*m);
    check(!m->hardware().stop() && m->read(qwc, 4) == 0 &&
          done->hardware.gif_path2_fifo.count == 0, "DIRECT retry failed after queue drained");
    m->restore(*stalled);
    m->write(0x1000e000, 4, 0);
    m->write(0x10003000, 4, 0);
    m->advance(4);
    m->write(0x1000e000, 4, 1);
    m->advance(20);
    check(*snap(*m) == *done, "partial assembler replay duplicated data");

    auto f = std::make_unique<Memory>();
    single(*f, 0x06008000);
    f->write_quadword(0x10006000, {0x1000000000008001ULL, 14});
    f->write_quadword(0x10006000, {0x1234, 1});
    single(*f, 0x11ffffff);
    check(f->read(qwc, 4) == 0, "FLUSH incorrectly waited independent PATH3");
    single(*f, 0x13ffffff);
    check(f->read(qwc, 4) == 1 && snap(f->hardware())->vif_transport.wait == VifWait::gif,
          "FLUSHA ignored masked PATH3 request");
    const auto waiting = snap(*f);
    f->advance(4);
    check(f->read(qwc, 4) == 1, "masked FLUSHA busy-loop completed spuriously");
    f->restore(*waiting);
}

void flush_is_not_finish() {
    auto m = std::make_unique<Memory>();
    direct(*m, {{0x1000000000008005ULL, 14},
        {(1ULL << 16) | (32ULL << 32) | (1ULL << 48), 0x50},
        {0, 0x51}, {(1ULL << 32) | 32, 0x52}, {2, 0x53}, {0, 0x61}});
    m->advance(8);
    check(!m->hardware().stop() && m->read(qwc, 4) == 0, "copy setup DIRECT transfer failed");
    single(*m, 0x11ffffff);
    check(!m->hardware().stop() && m->read(qwc, 4) == 0 &&
          m->hardware().graphics().transfer.active && m->hardware().graphics().finish_pending &&
          m->read(0x12001000, 8) == 0, "FLUSH incorrectly waited GS copy/FINISH fence");
    m->advance(32);
    check(m->read(0x12001000, 8) == 2, "copy FINISH fence did not complete independently");
}

void original_dma_guest() {
    auto system = std::make_unique<System>();
    auto& s = *system;
    auto& m = s.memory();
    std::vector<Qword> packet {{0x1000000000008004ULL, 14}, {1ULL << 48, 0x50},
        {0, 0x51}, {(1ULL << 32) | 32, 0x52}, {0, 0x53}, {0x0800000000008008ULL, 0}};
    for (unsigned n = 0; n < 32; n += 4) packet.push_back(pixels(n + 1));
    packet.insert(packet.end(), {{0x1000000000008005ULL, 14}, {1ULL << 16, 0x50},
        {0, 0x51}, {(1ULL << 32) | 32, 0x52}, {1, 0x53}, {0, 0x61}});
    m.write_quadword(0x3000, {0x1300000006008000ULL,
        std::uint64_t{0x50000000U | static_cast<unsigned>(packet.size())} << 32});
    for (unsigned n = 0; n < packet.size(); ++n) m.write_quadword(0x3010 + n * 16, packet[n]);
    m.write_quadword(0x3010 + static_cast<unsigned>(packet.size()) * 16,
                     {0, 0x1100000000000000ULL});
    std::vector<std::uint32_t> code {
        0x3c081001, 0x34090001, 0xad09e000, 0x34093000, 0xad099010,
        0x34090000U | static_cast<unsigned>(packet.size() + 2), 0xad099020,
        0x34090181, 0xad099000, 0x8d099020, 0x1520fffe, 0
    };
    const std::uint32_t setup[] {
        0x3c081200, 0x34090002, 0xfd091000, // acknowledge FINISH
        0x3c081000, 0x3c090080, 0xad093c00, // FDR1
        0x3c081200, 0x34090001, 0xfd091040, // BUSDIR1
        0x3c081001, 0xad09e000, 0x34090002, 0xad09e010, 0x3c090002, 0xad09e010,
        0x34095000, 0xad099010, 0x34090008, 0xad099020, 0x34090100, 0xad099000,
        0x8d099020, 0x1520fffe, 0, // wait QWC0
        0x340a5000
    };
    code.insert(code.end(), std::begin(setup), std::end(setup));
    for (unsigned n = 0; n < 32; ++n) {
        code.push_back(0x8d4b0000 | (n * 4));
        code.push_back(0x396b0000 | (n + 1)); // XORI with independent expected pixel.
        code.push_back(0x022b8825); // OR r17,r17,r11 accumulates mismatches.
    }
    const std::uint32_t forward[] {0x3c081000, 0xad003c00, 0x3c081200, 0xfd001040,
                                  0x3c081001, 0x3c090002, 0xad09e010, 0x34096000, 0xad099010, 0x34090001, 0xad099020,
        0x34090181, 0xad099000, 0x8d099020, 0x1520fffe, 0,
        0x3c081000, 0x35086000, 0x34094000};
    m.write_quadword(0x6000, {0, 0x0600000000000000ULL});
    code.insert(code.end(), std::begin(forward), std::end(forward));
    constexpr Qword sprite[] {{0x1000000000008005ULL, 14}, {6, 0},
        {0x10000, 0x4c}, {0x00abcdef, 1}, {0, 5}, {0x00100010, 5}};
    for (unsigned n = 0; n < std::size(sprite); ++n) {
        m.write_quadword(0x4000 + n * 16, sprite[n]);
        code.push_back(0x792a0000 | (n * 16));
        code.push_back(0x7d0a0000);
    }
    code.insert(code.end(), {0x24120001, 0x1000ffff, 0});
    // Handler outside guest code; BEV interrupt entry redirected to0x200 requires a jump.
    m.write(0x200, 4, 0x08000400);
    m.write(0x204, 4, 0);
    constexpr std::uint32_t handler[] {0x3c1a1001, 0x341b0002, 0xaf5be010,
                                      0x26100001, 0x42000018};
    for (unsigned n = 0; n < std::size(handler); ++n) m.write(0x1000 + n * 4, 4, handler[n]);
    auto cpu = snap(s.cpu());
    cpu->pc = 0x800;
    cpu->next_pc = 0x804;
    cpu->cop0.status = 0x10801;
    s.cpu().restore(*cpu);
    // Keep guest body separate from architectural vector0x200.
    for (unsigned n = 0; n < code.size(); ++n) m.write(0x800 + n * 4, 4, code[n]);
    check(s.run(30).budget_exhausted, "readback guest initial budget");
    const auto partial = snap(s);
    std::vector<InstructionTrace> a, b;
    const auto result = s.run(250, &a);
    check(result.budget_exhausted && !m.hardware().stop() &&
          s.cpu().state().gpr[16].low == 1 && s.cpu().state().gpr[17].low == 0 &&
          s.cpu().state().gpr[18].low == 1 && !(m.read(vifstat, 4) & (1U << 23)) &&
          m.hardware().graphics().pixels[0] == 0x00abcdef,
          "DIRECT guest reverse DMA verification/INT1/forward restore");
    const auto done = snap(s);
    s.restore(*partial);
    check(s.run(111, &b).budget_exhausted && s.run(139, &b).budget_exhausted &&
          *snap(s) == *done && a == b, "DIRECT guest reverse DMA replay");
}
}
int main() {
    try {
        literal_path2_pixels();
        alignment_counts_and_tte();
        ownership_and_mask();
        assembler_backpressure_and_flush();
        flush_is_not_finish();
        original_dma_guest();
        std::cout << "VIF DIRECT tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
