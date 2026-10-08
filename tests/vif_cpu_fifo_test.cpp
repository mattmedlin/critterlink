#include "critterlink/system.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace critterlink;
using Qword = std::array<std::uint64_t, 2>;
constexpr std::uint32_t fifo = 0x10005000, vifstat = 0x10003c00;
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
unsigned queued(Memory& m) { return static_cast<unsigned>((m.read(vifstat, 4) >> 24) & 31); }
void start(Memory& m, unsigned count = 1) {
    m.write(0x1000e000, 4, 1);
    m.write(0x10009010, 4, 0x3000);
    m.write(0x10009020, 4, count);
    m.write(0x10009000, 4, 0x181);
}
void busy(Memory& m) {
    auto state = snap(m);
    for (unsigned n = 0; n < 32; ++n) state->hardware.vector.micro[n] = {0x10000000, 0x000002ff};
    state->hardware.vector.micro[30][1] |= 0x40000000;
    state->hardware.vector.running = true;
    m.restore(*state);
}

void cpu_decode_and_aliases() {
    auto m = std::make_unique<Memory>();
    m->write_quadword(0x90005ff0, {0x06008000, 0});
    check(queued(*m) == 1 && !snap(m->hardware())->path3_masked,
          "CPU SQ decoded synchronously instead of enqueueing");
    m->advance(1);
    check(queued(*m) == 0 && snap(m->hardware())->path3_masked, "CPU mask command did not consume");
    m->write_quadword(0xb0005010, {0x06000000, 0});
    m->advance(1);
    check(!snap(m->hardware())->path3_masked, "CPU FIFO alias unmask");
    for (unsigned width : {1U, 2U, 4U, 8U}) rejects([&] { m->write(fifo, width, 0); });
    rejects([&] { m->write_quadword(fifo + 1, {0, 0}); });
    m->write_quadword(fifo, {0x7f00000006008000ULL, 0});
    check(!m->hardware().stop(), "invalid queued command failed synchronously");
    m->advance(1);
    const auto failed = snap(m->hardware());
    check(m->hardware().stop() && failed->path3_masked &&
          failed->vif_input_fifo.count == 1 &&
          failed->vif_input_fifo.entries[failed->vif_input_fifo.head].cursor == 1,
          "asynchronous invalid command did not retain sequential effects/cursor");
}

void literal_payloads() {
    auto m = std::make_unique<Memory>();
    m->write_quadword(fifo, {0x4a02000000000000ULL, 0x400002ff10010007ULL});
    m->write_quadword(fifo, {0x000002ff10000000ULL, 0x6c01000100000000ULL});
    m->write_quadword(fifo, {0x0000002200000011ULL, 0x0000004400000033ULL});
    m->advance(3);
    check(!m->hardware().stop() && m->hardware().vector().micro[0] ==
          std::array<std::uint32_t, 2>{0x10010007, 0x400002ff} &&
          m->hardware().vector().data[1] == std::array<std::uint32_t, 4>{0x11, 0x22, 0x33, 0x44},
          "CPU MPG/UNPACK physical lane or payload ordering");
    m->write_quadword(fifo, {0, 0x5000000200000000ULL});
    m->write_quadword(fifo, {0x1000000000008001ULL, 14});
    m->write_quadword(fifo, {0x12345678, 1});
    m->advance(4);
    check(m->hardware().graphics().rgba == 0x12345678 && !m->hardware().stop(),
          "CPU DIRECT literal A+D payload");
}

void dma_order_abort_and_latching() {
    auto m = std::make_unique<Memory>();
    busy(*m);
    m->write_quadword(fifo, {0x10000000, 0});
    m->write_quadword(0x3000, {0x06008000, 0});
    start(*m, 2);
    m->write_quadword(0x3010, {0x06000000, 0});
    m->advance(1);
    check(queued(*m) == 2 && m->read(0x10009020, 4) == 1 &&
          !snap(m->hardware())->path3_masked, "DMA bypassed earlier CPU wait");
    m->write_quadword(0x3000, {0x7f000000, 0});
    m->write(0x10009000, 4, 0);
    m->write(0x1000e000, 4, 0);
    const auto partial = snap(*m);
    m->advance(35);
    check(!m->hardware().stop() && snap(m->hardware())->path3_masked &&
          m->read(0x10009020, 4) == 1 && queued(*m) == 0,
          "abort/DMAE lost accepted data, reread RAM or accepted canceled data");
    const auto done = snap(*m);
    m->restore(*partial);
    m->advance(13);
    m->advance(22);
    check(*snap(*m) == *done, "CPU/DMA shared queue replay");

    auto completed = std::make_unique<Memory>();
    busy(*completed);
    completed->write_quadword(0x3000, {0x10000000, 0});
    start(*completed);
    completed->advance(1);
    check(completed->read(0x10009020, 4) == 0 && (completed->read(0x1000e010, 4) & 2) &&
          queued(*completed) == 1, "DMA completion incorrectly waited VIF execution");
    const auto waiting = snap(*completed);
    completed->restore(*waiting);
    check(*snap(*completed) == *waiting, "DMA-completed VU-wait snapshot rejected");
}

void full_queue_sq_retry_and_snapshots() {
    auto s = std::make_unique<System>();
    auto& m = s->memory();
    busy(m);
    m.write_quadword(fifo, {0x10000000, 0});
    for (unsigned n = 1; n < 16; ++n) m.write_quadword(fifo, {0, 0});
    const auto full = snap(m);
    rejects<MemoryStall>([&] { m.write_quadword(fifo, {0x06008000, 0}); });
    check(*snap(m) == *full, "full FIFO SQ was not atomic");
    rejects([&] { m.write(vifstat, 4, 1U << 23); });
    rejects([&] { m.write(0x12001040, 8, 1); });
    m.write(0x10003000, 4, 1);
    check(queued(m) == 16, "GIF reset discarded VIF input");
    m.write(0, 4, 0x7d090000); // SQ r9,0(r8).
    m.write(4, 4, 0x1000ffff);
    m.write(8, 4, 0);
    auto cpu = snap(s->cpu());
    cpu->gpr[8].low = fifo;
    cpu->gpr[9] = {0x06008000, 0};
    s->cpu().restore(*cpu);
    const auto pending = snap(*s);
    std::vector<InstructionTrace> a, b;
    check(s->run(1, &a).retired == 0 && s->cpu().state().pc == 0 && queued(m) == 16,
          "full FIFO SQ retired before space existed");
    check(s->run(70, &a).budget_exhausted && !m.hardware().stop() &&
          snap(m.hardware())->path3_masked && queued(m) == 0, "stalled CPU SQ did not deliver once");
    check(std::count_if(a.begin(), a.end(), [](const InstructionTrace& entry) {
        return entry.pc == 0 && entry.retired;
    }) == 1, "retried CPU SQ retired more than once");
    const auto done = snap(*s);
    s->restore(*pending);
    check(s->run(20, &b).budget_exhausted && s->run(51, &b).budget_exhausted &&
          *snap(*s) == *done && a == b, "CPU-only wait SQ retry replay");
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
        auto bad = std::unique_ptr<MemoryState>(new MemoryState(*full));
        auto& queue = bad->hardware.vif_input_fifo;
        if (mutation == 0) queue.count = 17;
        if (mutation == 1) queue.entries[queue.head].cursor = 4;
        if (mutation == 2) queue.entries[(queue.head + 1) % 16].cursor = 1;
        if (mutation == 3) bad->hardware.vif_fdr = true;
        rejects<std::invalid_argument>([&] { m.restore(*bad); });
        check(*snap(*s) == *done, "malformed ingress restore mutated system");
    }
}

Qword pixels(unsigned first) {
    return {first | (std::uint64_t{first + 1} << 32),
            (first + 2) | (std::uint64_t{first + 3} << 32)};
}
void original_cpu_unmask_guest() {
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
                                  0x3c081000, 0x35085000, 0x34096000, 0x792a0000, 0x7d0a0000,
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
          "CPU FIFO guest reverse DMA verification/INT1/forward restore");
    const auto done = snap(s);
    s.restore(*partial);
    check(s.run(111, &b).budget_exhausted && s.run(139, &b).budget_exhausted &&
          *snap(s) == *done && a == b, "CPU FIFO guest reverse DMA replay");
}
}
int main() {
    try {
        cpu_decode_and_aliases();
        literal_payloads();
        dma_order_abort_and_latching();
        full_queue_sq_retry_and_snapshots();
        original_cpu_unmask_guest();
        std::cout << "VIF CPU FIFO tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
