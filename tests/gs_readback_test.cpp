#include "critterlink/system.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace critterlink;
using Qword = std::array<std::uint64_t, 2>;
constexpr std::uint32_t fifo = 0x10005000, vifstat = 0x10003c00;
constexpr std::uint32_t busdir = 0x12001040, csr = 0x12001000;
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
void command(Memory& m, unsigned address, std::uint64_t value = 0) {
    m.write_quadword(0x10006000, {0x1000000000008001ULL, 14});
    m.write_quadword(0x10006000, {value, address});
    m.advance(2);
}
void upload(Memory& m, unsigned width, unsigned height, unsigned bp = 0, unsigned bw = 1) {
    command(m, 0x50, (std::uint64_t{bp} << 32) | (std::uint64_t{bw} << 48));
    command(m, 0x51);
    command(m, 0x52, width | (std::uint64_t{height} << 32));
    command(m, 0x53, 0);
    m.write_quadword(0x10006000, {(0x08000000ULL << 32) | 0x8000 | (width * height / 4), 0});
    m.advance(1);
    for (unsigned n = 0; n < width * height; n += 4) {
        m.write_quadword(0x10006000, pixels(n + 1));
        m.advance(1);
    }
}
void prepare(Memory& m, unsigned width, unsigned height, unsigned bp = 0, unsigned bw = 1,
             unsigned x = 0, unsigned y = 0) {
    command(m, 0x50, bp | (std::uint64_t{bw} << 16));
    command(m, 0x51, x | (std::uint64_t{y} << 16) | (3ULL << 59));
    command(m, 0x52, width | (std::uint64_t{height} << 32));
    command(m, 0x53, 1);
    command(m, 0x61);
    check(m.read(csr, 8) == 2, "FINISH waited for unread local-to-host pixels");
    m.write(csr, 8, 2);
    m.write(vifstat, 4, 1U << 23);
    m.write(busdir, 8, 1);
}
unsigned queued(Memory& m) { return static_cast<unsigned>((m.read(vifstat, 4) >> 24) & 31); }

void direction_setup_gap() {
    auto memory = std::make_unique<Memory>();
    auto& m = *memory;
    m.write(vifstat, 4, 1U << 23); // SDK setup order permits FDR before BUSDIR.
    m.write(madr, 4, 0x3000);
    m.write(qwc, 4, 1);
    const auto before = snap(m);
    rejects([&] { m.write(chcr, 4, 0x101); });
    check(*snap(m) == *before, "FDR1/BUSDIR0 accepted forward VIF DMA");
    auto bad = std::unique_ptr<MemoryState>(new MemoryState(*before));
    bad->hardware.vif_dma.chcr = 0x101;
    rejects<std::invalid_argument>([&] { m.restore(*bad); });
    check(*snap(m) == *before, "forward DMA/FDR snapshot rejection mutated state");
}

void raster_and_odd_width() {
    auto memory = std::make_unique<Memory>();
    auto& m = *memory;
    upload(m, 64, 4, 1);
    // Independent source words prove uploaded storage crosses PSMCT32 columns/blocks.
    check(m.hardware().graphics().vram[64] == 1 && m.hardware().graphics().vram[128] == 9 &&
          m.hardware().graphics().vram[80] == 129, "literal uploaded swizzled source");
    prepare(m, 3, 4, 1, 1, 7, 0);
    m.advance(4);
    constexpr std::array<Qword, 3> expected {{
        {8ULL | (9ULL << 32), 10ULL | (72ULL << 32)},
        {73ULL | (74ULL << 32), 136ULL | (137ULL << 32)},
        {138ULL | (200ULL << 32), 201ULL | (202ULL << 32)}
    }};
    check(queued(m) == 3, "odd-width producer qword count");
    for (unsigned n = 0; n < 3; ++n) {
        const std::uint32_t address = n == 0 ? fifo : n == 1 ? 0x90005ff0 : 0xb0005010;
        check(m.read_quadword(address) == expected[n], "raster readback or FIFO alias ordering");
    }
    check(queued(m) == 0 && m.read(csr, 8) == 0, "read completion invented FINISH");
    m.write(vifstat, 4, 0);
    m.write(busdir, 8, 0);
    command(m, 0x61);
    check(m.read(csr, 8) == 2, "forward path did not resume");
}

void queue_pause_guards_and_replay() {
    auto memory = std::make_unique<Memory>();
    auto& m = *memory;
    upload(m, 64, 2);
    prepare(m, 64, 2);
    m.write(0x1000e000, 4, 0);
    m.advance(20);
    check(queued(m) == 16, "producer did not stop at capacity16 while DMAE disabled");
    auto changed_source = snap(m);
    changed_source->hardware.graphics.vram[0] = 999;
    changed_source->hardware.graphics.pixels[0] = 999;
    m.restore(*changed_source); // Already queued qwords must retain the old source value.
    const auto full = snap(m);
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
        auto bad = std::unique_ptr<MemoryState>(new MemoryState(*full));
        if (mutation == 0) bad->hardware.vif_readback_fifo.count = 17;
        if (mutation == 1) bad->hardware.vif_readback_fifo.head = 16;
        if (mutation == 2) bad->hardware.vif_fdr = false;
        if (mutation == 3) bad->hardware.graphics.transfer.local_readback = false;
        rejects<std::invalid_argument>([&] { m.restore(*bad); });
        check(*snap(m) == *full, "malformed reverse queue restore changed state");
    }
    for (unsigned width : {1U, 2U, 4U, 8U}) {
        rejects([&] { (void)m.read(fifo, width); });
    }
    rejects([&] { m.write_quadword(0x10006000, {0, 0}); });
    rejects([&] { (void)m.read_quadword(0x10006000); });
    rejects([&] { m.write(vifstat, 4, 0); });
    rejects([&] { m.write(busdir, 8, 0); });
    check(*snap(m) == *full, "reverse guard consumed data or mutated state");
    check(m.read_quadword(fifo) == pixels(1), "first full-queue qword");
    m.advance(1);
    const auto partial = snap(m);
    std::vector<Qword> expected;
    for (unsigned n = 1; n < 32; ++n) {
        expected.push_back(m.read_quadword(fifo));
        check(expected.back() == pixels(n * 4 + 1), "FIFO producer/consumer ordering");
        m.advance(1);
    }
    const auto done = snap(m);
    m.restore(*partial);
    for (const auto& value : expected) {
        check(m.read_quadword(fifo) == value, "partial queue replay data");
        m.advance(1);
    }
    check(*snap(m) == *done, "partial queue full state replay");
}

void dma_errors_and_accounting() {
    auto memory = std::make_unique<Memory>();
    auto& m = *memory;
    upload(m, 32, 1);
    prepare(m, 32, 1);
    m.write(madr, 4, 0x5000);
    for (unsigned count : {0U, 7U, 16U}) {
        m.write(qwc, 4, count);
        const auto before = snap(m);
        rejects([&] { m.write(chcr, 4, 0x100); });
        check(*snap(m) == *before, "reverse DMA invalid QWC partially launched");
    }
    m.write(qwc, 4, 8);
    rejects([&] { m.write(chcr, 4, 0x104); });
    rejects([&] { m.write(chcr, 4, 0x101); });
    m.write(madr, 4, 0x02000000);
    m.write(chcr, 4, 0x100);
    m.advance(8); // DMAE0 allows production but not consumption.
    check(queued(m) == 8 && m.read(qwc, 4) == 8, "DMAE pause consumed reverse data");
    const auto buffered = snap(m);
    m.write(0x1000e000, 4, 1);
    m.advance(1);
    check((m.read(0x1000e010, 4) & 0x8002) == 0x8002 && queued(m) == 8 &&
          m.read(qwc, 4) == 8 && !(m.read(chcr, 4) & 0x100),
          "destination bus error popped FIFO before validating whole qword");
    const auto faulted = snap(m);
    m.restore(*faulted);
    check(*snap(m) == *faulted, "reverse DMA fault snapshot invalid");
    m.restore(*buffered);
    m.write(chcr, 4, 0);
    m.write(madr, 4, 0x5000);
    m.write(chcr, 4, 0x100);
    m.write(0x1000e000, 4, 1);
    m.advance(8);
    check(m.read(qwc, 4) == 0 && m.read(madr, 4) == 0x5080 && queued(m) == 0,
          "reverse DMA register accounting");
    for (unsigned n = 0; n < 32; ++n) check(m.read(0x5000 + n * 4, 4) == n + 1, "DMA RAM pixel");
}

void cpu_empty_fifo_stall() {
    auto system = std::make_unique<System>();
    auto& s = *system;
    auto& m = s.memory();
    upload(m, 4, 1);
    prepare(m, 4, 1);
    // No producer tick has occurred since BUSDIR switched: first LQ must retry.
    m.write(0, 4, 0x79090000); // LQ r9,0(r8).
    m.write(4, 4, 0x1000ffff);
    m.write(8, 4, 0);
    auto cpu = snap(s.cpu());
    cpu->gpr[8].low = fifo;
    cpu->gpr[9] = {0xaaaaaaaa, 0xbbbbbbbb};
    s.cpu().restore(*cpu);
    const auto empty = snap(s);
    std::vector<InstructionTrace> a, b;
    check(s.run(1, &a).retired == 0 && s.cpu().state().pc == 0 &&
          s.cpu().state().gpr[9].low == 0xaaaaaaaa, "empty FIFO LQ partially retired");
    check(s.run(1, &a).retired == 1 && s.cpu().state().gpr[9].low == pixels(1)[0] &&
          s.cpu().state().gpr[9].high == pixels(1)[1], "stalled LQ did not retry once");
    const auto done = snap(s);
    s.restore(*empty);
    check(s.run(1, &b).retired == 0 && s.run(1, &b).retired == 1 &&
          *snap(s) == *done && a == b, "empty FIFO stall snapshot replay");
}

void original_guest_dma() {
    auto system = std::make_unique<System>();
    auto& s = *system;
    auto& m = s.memory();
    std::vector<Qword> packet {{0x1000000000008004ULL, 14}, {1ULL << 48, 0x50},
        {0, 0x51}, {(1ULL << 32) | 32, 0x52}, {0, 0x53}, {0x0800000000008008ULL, 0}};
    for (unsigned n = 0; n < 32; n += 4) packet.push_back(pixels(n + 1));
    packet.insert(packet.end(), {{0x1000000000008005ULL, 14}, {1ULL << 16, 0x50},
        {0, 0x51}, {(1ULL << 32) | 32, 0x52}, {1, 0x53}, {0, 0x61}});
    for (unsigned n = 0; n < packet.size(); ++n) m.write_quadword(0x3000 + n * 16, packet[n]);
    std::vector<std::uint32_t> code {0x3c081000, 0x35086000, 0x34093000};
    for (unsigned n = 0; n < packet.size(); ++n) {
        code.push_back(0x792a0000 | (n * 16));
        code.push_back(0x7d0a0000);
    }
    const std::uint32_t setup[] {
        0x3c081200, 0x34090002, 0xfd091000, // acknowledge FINISH
        0x3c081000, 0x3c090080, 0xad093c00, // FDR1
        0x3c081200, 0x34090001, 0xfd091040, // BUSDIR1
        0x3c081001, 0xad09e000, 0x3c090002, 0xad09e010,
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
                                  0x3c081000, 0x35086000, 0x34094000};
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
    check(s.run(60).budget_exhausted, "readback guest initial budget");
    const auto partial = snap(s);
    std::vector<InstructionTrace> a, b;
    const auto result = s.run(250, &a);
    check(result.budget_exhausted && !m.hardware().stop() &&
          s.cpu().state().gpr[16].low == 1 && s.cpu().state().gpr[17].low == 0 &&
          s.cpu().state().gpr[18].low == 1 && !(m.read(vifstat, 4) & (1U << 23)) &&
          m.hardware().graphics().pixels[0] == 0x00abcdef,
          "guest reverse DMA verification/INT1/forward restore");
    const auto done = snap(s);
    s.restore(*partial);
    check(s.run(111, &b).budget_exhausted && s.run(139, &b).budget_exhausted &&
          *snap(s) == *done && a == b, "guest reverse DMA replay");
}
}
int main() {
    try {
        direction_setup_gap();
        raster_and_odd_width();
        queue_pause_guards_and_replay();
        dma_errors_and_accounting();
        cpu_empty_fifo_stall();
        original_guest_dma();
        std::cout << "GS readback tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
