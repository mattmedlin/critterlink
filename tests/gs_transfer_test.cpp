#include "critterlink/system.hpp"
#include "critterlink/gs_memory.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace critterlink;
using Words = std::array<std::uint32_t, 4>;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class T> auto snap(const T& object) {
    using S = std::remove_cvref_t<decltype(object.state())>;
    return std::unique_ptr<S>(new S(object.state()));
}
template<class F> void rejects(F fn) {
    try { fn(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("missing rejection");
}
void packed(Graphics& g, unsigned count) {
    g.submit_qword({count | 0x8000U, 0x10000000, 14, 0});
}
void reg(Graphics& g, unsigned address, std::uint64_t value) {
    g.submit_qword({static_cast<std::uint32_t>(value),
                    static_cast<std::uint32_t>(value >> 32), address, 0});
}
void write(Graphics& g, unsigned address, std::uint64_t value) {
    packed(g, 1);
    reg(g, address, value);
}
void image(Graphics& g, unsigned count, bool ignored_fields = false) {
    g.submit_qword({count | 0x8000U, ignored_fields ? 0xfbffc000U : 0x08000000U,
                    ignored_fields ? 0xffffffffU : 0U, ignored_fields ? 0xffffffffU : 0U});
}
void setup(Graphics& g, unsigned width, unsigned height, unsigned bp = 0,
           unsigned bw = 1, unsigned x = 0, unsigned y = 0) {
    packed(g, 4);
    reg(g, 0x50, (std::uint64_t{bp} << 32) | (std::uint64_t{bw} << 48));
    reg(g, 0x51, (std::uint64_t{x} << 32) | (std::uint64_t{y} << 48));
    reg(g, 0x52, width | (std::uint64_t{height} << 32));
    reg(g, 0x53, 0);
}

void literal_addresses() {
    struct Vector { unsigned bp, bw, x, y, bytes; };
    // Transcribed from Sony GS manual p164; BP carry example follows p171.
    constexpr Vector vectors[] {
        {0,1,0,0,0}, {0,1,1,0,4}, {0,1,2,0,16}, {0,1,0,1,8},
        {0,1,0,2,64}, {0,1,7,7,252}, {0,1,8,0,256}, {0,1,16,0,1024},
        {0,1,32,0,4096}, {0,1,0,8,512}, {0,1,0,16,2048},
        {0,1,63,31,8188}, {0,2,64,0,8192}, {0,2,0,32,16384},
        {1,1,0,0,256}, {1,1,56,24,8192}
    };
    for (const auto& v : vectors) {
        check(gs_psmct32_address(v.bp, v.bw, v.x, v.y) == v.bytes,
              "literal PSMCT32 address mismatch");
    }
    for (unsigned bp : {0U, 1U, 31U}) {
        for (unsigned bw : {1U, 2U, 32U}) {
            std::array<bool, 2048> seen{};
            for (unsigned y = 0; y < 32; ++y) {
                for (unsigned x = 0; x < 64; ++x) {
                    const auto address = gs_psmct32_address(bp, bw, x, y);
                    check(address >= bp * 256 && address < bp * 256 + 8192 && address % 4 == 0,
                          "page swizzle escaped contiguous block sequence");
                    const auto index = (address - bp * 256) / 4;
                    check(!seen[index], "page swizzle aliases pixels");
                    seen[index] = true;
                }
            }
        }
    }
}

void ignored_empty_tags_and_alias() {
    auto g = std::make_unique<Graphics>();
    setup(*g, 4, 1);
    const auto before = snap(*g);
    // NLOOP=0 ignores even reserved bits, PRE/PRIM, FLG and descriptors.
    for (unsigned mode = 0; mode < 4; ++mode) {
        g->submit_qword({0xffff8000U, (0xffffffffU & ~0x0c000000U) | (mode << 26),
                        0xffffffffU, 0xffffffffU});
        check(g->state() == *before, "empty GIF tag applied ignored fields or PRE");
    }
    g->submit_qword({0x8001, 0x0c000000, 0, 0}); // FLG3 is IMAGE alias.
    g->submit_qword({41, 42, 43, 44});
    check(g->state().vram[0] == 41 && g->state().vram[1] == 42 &&
          g->state().vram[4] == 43 && g->state().vram[5] == 44,
          "FLG3 IMAGE alias did not upload");
}

void gif_reset_preserves_upload() {
    auto graphics = std::make_unique<Graphics>();
    setup(*graphics, 8, 1);
    auto hardware = std::make_unique<Hardware>();
    auto seed = snap(*hardware);
    seed->graphics = graphics->state();
    hardware->restore(*seed);
    check(hardware->write_quadword(0x10006000, {0x8002, 0x08000000, 0, 0}) &&
          hardware->write_quadword(0x10006000, {1, 2, 3, 4}), "GIF upload enqueue");
    hardware->advance(2, {});
    check(hardware->graphics().remaining == 1 && hardware->graphics().transfer.cursor == 4,
          "mid-IMAGE upload setup");
    hardware->write(0x10003000, 8); // Pause before queuing data that reset must discard.
    check(hardware->write_quadword(0x10006000, {99, 99, 99, 99}), "paused FIFO enqueue");
    hardware->write(0x10003000, 1);
    const auto reset = snap(*hardware);
    check(reset->gif_fifo.count == 0 && reset->graphics.remaining == 0 &&
          reset->graphics.transfer.cursor == 4 && reset->graphics.transfer.active &&
          reset->graphics.vram[0] == 1 && reset->graphics.vram[5] == 4,
          "GIF reset discarded GS upload state or retained FIFO");
    hardware->write(0x10003000, 0);
    check(hardware->write_quadword(0x10006000, {0x8001, 0x08000000, 0, 0}) &&
          hardware->write_quadword(0x10006000, {5, 6, 7, 8}), "continuation enqueue");
    hardware->advance(2, {});
    check(!hardware->stop() && !hardware->graphics().transfer.active &&
          hardware->graphics().vram[8] == 5 && hardware->graphics().vram[13] == 8,
          "new IMAGE did not continue preserved upload cursor");
}

void swizzled_upload() {
    auto g = std::make_unique<Graphics>();
    setup(*g, 128, 34, 1, 2);
    image(*g, 128 * 34 / 4);
    for (unsigned pixel = 0; pixel < 128 * 34; pixel += 4) {
        g->submit_qword({pixel + 1, pixel + 2, pixel + 3, pixel + 4});
    }
    // Raw VRAM assertions avoid using the production address helper as an oracle.
    struct Probe { unsigned byte_offset, value; };
    constexpr Probe probes[] {
        {256,1}, {260,2}, {272,3}, {264,129}, {320,257}, {508,904},
        {512,9}, {1280,17}, {4352,33}, {768,1025}, {2304,2049},
        {8444,4032}, {8448,65}, {16640,4097}
    };
    for (const auto& p : probes) {
        check(g->state().vram[p.byte_offset / 4] == p.value, "raw swizzled upload value");
    }
    check(g->state().vram[0] == 0 && g->state().vram.back() == 0,
          "upload touched unrelated VRAM");
}

void transport_latching_and_replay() {
    auto g = std::make_unique<Graphics>();
    write(*g, 0x54, 0xffffffffffffffffULL);
    check(g->state().vram[0] == 0, "inactive HWREG wrote VRAM");
    setup(*g, 2, 1);
    image(*g, 1, true);
    g->submit_qword({11, 22, 0xffffffff, 0xffffffff});
    check(g->state().vram[0] == 11 && g->state().vram[1] == 22 &&
          g->state().vram[4] == 0, "IMAGE ignored fields or padding");

    setup(*g, 4, 2, 0, 1, 32, 8);
    image(*g, 2);
    g->submit_qword({101, 102, 103, 104});
    const auto mid_image = snap(*g);
    g->submit_qword({201, 202, 203, 204});
    const auto image_done = snap(*g);
    g->restore(*mid_image);
    g->submit_qword({201, 202, 203, 204});
    check(g->state() == *image_done && g->state().vram[1152] == 101 &&
          g->state().vram[1154] == 201, "mid-IMAGE rectangle replay");

    setup(*g, 4, 1, 0, 1, 24, 0);
    write(*g, 0x54, 0x0000002200000011ULL);
    write(*g, 0x53, 0); // Restart an incomplete transfer at its first pixel.
    write(*g, 0x54, 0x0000004400000033ULL);
    check(g->state().vram[320] == 0x33 && g->state().vram[321] == 0x44 &&
          g->state().vram[324] == 0, "active restart did not reset upload cursor");
    write(*g, 0x53, 3);

    setup(*g, 8, 1, 0, 1, 0, 2);
    write(*g, 0x54, 0x0000000200000001ULL);
    image(*g, 1);
    g->submit_qword({3, 4, 5, 6});
    const auto partial = snap(*g);
    write(*g, 0x51, std::uint64_t{8} << 32);
    write(*g, 0x52, 2 | (std::uint64_t{1} << 32));
    image(*g, 1);
    g->submit_qword({7, 8, 0xffffffff, 0xffffffff});
    check(g->state().vram[16] == 1 && g->state().vram[17] == 2 &&
          g->state().vram[28] == 7 && g->state().vram[29] == 8 &&
          g->state().vram[64] == 0, "parameter writes changed active upload");
    const auto completed = snap(*g);
    g->restore(*partial);
    write(*g, 0x51, std::uint64_t{8} << 32);
    write(*g, 0x52, 2 | (std::uint64_t{1} << 32));
    image(*g, 1);
    g->submit_qword({7, 8, 0xffffffff, 0xffffffff});
    check(g->state() == *completed, "mixed HWREG IMAGE replay");
    write(*g, 0x53, 0);
    write(*g, 0x54, 0x0000006600000055ULL);
    check(g->state().vram[64] == 0x55 && g->state().vram[65] == 0x66,
          "TRXDIR restart did not latch current parameters");
    setup(*g, 4, 1, 0, 1, 16, 0);
    write(*g, 0x54, 0x000000bb000000aaULL);
    write(*g, 0x53, 3);
    write(*g, 0x54, 0xffffffffffffffffULL);
    check(g->state().vram[256] == 0xaa && g->state().vram[260] == 0,
          "deactivation failed to discard pending upload");
}

void rejected_configuration() {
    auto g = std::make_unique<Graphics>();
    setup(*g, 4, 1);
    // Readback/copy lack a configured source buffer here; direction4 is reserved.
    for (unsigned direction : {1U, 2U, 4U}) {
        packed(*g, 1);
        const auto before = snap(*g);
        rejects([&] { reg(*g, 0x53, direction); });
        check(g->state() == *before, "invalid transfer start changed active upload");
        reg(*g, 0x7f, 0);
    }
    // Restrictions are this implementation's supported profile, not hardware wrap claims.
    struct Bad { unsigned bp, bw, format, x, y, width, height; };
    constexpr Bad bad[] {
        {0,0,0,0,0,2,1}, {0,33,0,0,0,2,1}, {0,1,1,0,0,2,1},
        {0,1,2,0,0,2,1}, {0,1,0x30,0,0,2,1}, {0,1,0x13,0,0,2,1},
        {0,1,0,0,0,0,1}, {0,1,0,0,0,3,1}, {0,1,0,63,0,2,1},
        {0,1,0,0,2047,2,2}, {16383,1,0,56,24,2,1}
    };
    for (const auto& b : bad) {
        auto invalid = std::make_unique<Graphics>();
        packed(*invalid, 4);
        reg(*invalid, 0x50, (std::uint64_t{b.bp} << 32) |
            (std::uint64_t{b.bw} << 48) | (std::uint64_t{b.format} << 56));
        reg(*invalid, 0x51, (std::uint64_t{b.x} << 32) | (std::uint64_t{b.y} << 48));
        reg(*invalid, 0x52, b.width | (std::uint64_t{b.height} << 32));
        const auto before = snap(*invalid);
        rejects([&] { reg(*invalid, 0x53, 0); });
        check(invalid->state() == *before, "invalid transfer start was not atomic");
        invalid->restore(*before);
    }
    for (const auto address : {0x50U, 0x51U, 0x52U}) {
        packed(*g, 1);
        const auto before = snap(*g);
        rejects([&] { reg(*g, address, 0x8000000000000000ULL); });
        check(g->state() == *before, "reserved register bits mutated state");
        reg(*g, 0x7f, 0);
    }
    auto bad_state = snap(*g);
    bad_state->vram.pop_back();
    const auto original = snap(*g);
    rejects([&] { g->restore(*bad_state); });
    check(g->state() == *original, "invalid VRAM snapshot changed graphics");
}

void sprite_shares_storage() {
    auto g = std::make_unique<Graphics>();
    setup(*g, 2, 1);
    write(*g, 0x54, 0x9933445580123456ULL);
    packed(*g, 5);
    reg(*g, 0, 6);
    reg(*g, 0x4c, 0xff00000000010000ULL);
    reg(*g, 1, 0xffffffff);
    reg(*g, 5, 0);
    reg(*g, 5, 0x00100010);
    check(g->state().vram[0] == 0x80ffffff && g->state().pixels[0] == 0x80ffffff &&
          g->state().vram[1] == 0x99334455 && g->state().pixels[1] == 0x99334455,
          "masked sprite and upload do not share fixed framebuffer storage");
}

void guest_upload_and_replay() {
    auto system = std::make_unique<System>();
    auto& s = *system;
    auto& m = s.memory();
    // CPU SQ writes a five-qword register packet; channel2 transfers the IMAGE packet.
    constexpr Words registers[] {
        {0x8004, 0x10000000, 14, 0}, {0, 0x00010000, 0x50, 0},
        {0, 0, 0x51, 0}, {4, 1, 0x52, 0}, {0, 0, 0x53, 0}
    };
    for (unsigned n = 0; n < std::size(registers); ++n) {
        const auto& w = registers[n];
        m.write_quadword(0x3000 + n * 16, {w[0] | (std::uint64_t{w[1]} << 32),
                                                  w[2] | (std::uint64_t{w[3]} << 32)});
    }
    m.write_quadword(0x2000, {0x70000002, 0});
    m.write_quadword(0x2010, {0x0800000000008001ULL, 0});
    m.write_quadword(0x2020, {0x2233445511223344ULL, 0x4455667733445566ULL});
    constexpr std::uint32_t guest[] {
        0x3c081000, 0x35086000, 0x34093000,
        0x792a0000, 0x7d0a0000, 0x792a0010, 0x7d0a0000,
        0x792a0020, 0x7d0a0000, 0x792a0030, 0x7d0a0000,
        0x792a0040, 0x7d0a0000,
        0x3c081001, 0x34090001, 0xad09e000, 0x3c090004, 0xad09e010,
        0x34092000, 0xad09a030, 0x34090185, 0xad09a000, 0x1000ffff, 0
    };
    constexpr std::uint32_t handler[] {
        0x3c081001, 0x8d09e010, 0xac091000, 0x34090004, 0xad09e010,
        0x26100001, 0x42000018
    };
    for (unsigned n = 0; n < std::size(guest); ++n) m.write(n * 4, 4, guest[n]);
    for (unsigned n = 0; n < std::size(handler); ++n) m.write(0x200 + n * 4, 4, handler[n]);
    auto cpu = snap(s.cpu());
    cpu->cop0.status = 0x10801;
    s.cpu().restore(*cpu);
    check(s.run(22).budget_exhausted, "upload guest launch");
    const auto pending = snap(s);
    std::vector<InstructionTrace> a, b;
    check(s.run(35, &a).budget_exhausted && !m.hardware().stop() &&
          s.cpu().state().gpr[16].low == 1 && (m.read(0x1000, 4) & 4),
          "upload guest DMA interrupt");
    const auto& graphics = m.hardware().graphics();
    check(graphics.vram[0] == 0x11223344 && graphics.vram[1] == 0x22334455 &&
          graphics.vram[4] == 0x33445566 && graphics.vram[5] == 0x44556677,
          "guest CPU FIFO plus GIF chain upload raw VRAM");
    const auto completed = snap(s);
    s.restore(*pending);
    check(s.run(12, &b).budget_exhausted && s.run(23, &b).budget_exhausted &&
          *snap(s) == *completed && a == b, "upload guest replay");
}
}
int main() {
    try {
        literal_addresses();
        ignored_empty_tags_and_alias();
        gif_reset_preserves_upload();
        swizzled_upload();
        transport_latching_and_replay();
        rejected_configuration();
        sprite_shares_storage();
        guest_upload_and_replay();
        std::cout << "GS PSMCT32 transfer tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
