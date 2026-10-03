#include "critterlink/cdvd.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool caught = false;
    try { action(); } catch (const std::invalid_argument&) { caught = true; }
    check(caught, "expected rejected CDVD operation");
}
std::vector<std::uint8_t> media() {
    std::vector<std::uint8_t> bytes(4096);
    for (std::size_t n = 0; n < bytes.size(); ++n)
        bytes[n] = static_cast<std::uint8_t>((n * 17 + n / 2048 * 31) & 255);
    return bytes;
}
void command(critterlink::Cdvd& cd, std::uint32_t lsn, std::uint32_t count) {
    for (unsigned n = 0; n < 4; ++n) cd.write8(0x1f402005, static_cast<std::uint8_t>(lsn >> (n * 8U)));
    for (unsigned n = 0; n < 4; ++n) cd.write8(0x1f402005, static_cast<std::uint8_t>(count >> (n * 8U)));
    cd.write8(0x1f402005, 0); cd.write8(0x1f402005, 1); cd.write8(0x1f402005, 0);
    cd.write8(0x1f402004, 6);
}
void dma(critterlink::Cdvd& cd, std::uint32_t destination = 0x1000, unsigned count = 1) {
    cd.write8(0x1f402006, 0x80);
    cd.write32(0x1f8010f0, 0x8000);
    cd.write32(0x1f8010b0, destination);
    cd.write32(0x1f8010b4, (count * 16U << 16U) | 32U);
    cd.write32(0x1f8010b8, 0x41000200);
}
}
int main() {
    using critterlink::Cdvd;
    try {
        Cdvd cd;
        std::vector<std::uint8_t> ram(2U * 1024U * 1024U, 0xcc);
        check(cd.read8(0x1f40200f) == 0, "empty drive type");
        command(cd, 0, 1);
        check(cd.read8(0x1f402006) == 0x12 && cd.irq(), "missing media error");
        cd.mount(media());
        check(cd.read8(0x1f40200f) == 0x12 && cd.read8(0x1f402005) == 0x40,
            "mounted CD ready/type");
        rejects([&] { cd.mount(std::vector<std::uint8_t>(2047)); });
        const auto clean = cd.state();
        rejects([&] { cd.write8(0x1f402004, 8); });
        rejects([&] { cd.write32(0x1f402004, 6); });
        rejects([&] { cd.write8(0x1f402018, 0); });
        rejects([&] { cd.write8(0x1f402006, 0x86); });
        check(cd.state() == clean, "unsupported register changed drive state");
        command(cd, 1, 2);
        check(cd.read8(0x1f402006) == 0x32 && !cd.state().reading, "end-of-media error");
        cd.write8(0x1f402008, 2);
        cd.write8(0x1f402004, 6);
        check(cd.read8(0x1f402006) == 0x22, "missing parameters error");
        cd.write8(0x1f402008, 2);
        dma(cd);
        command(cd, 1, 1);
        check(cd.read8(0x1f402005) == 0x80 && cd.read8(0x1f40200a) == 6,
            "busy read status");
        rejects([&] { cd.mount(media()); });
        cd.tick(ram);
        const auto checkpoint = cd.state();
        const auto ram_checkpoint = ram;
        check(checkpoint.transferred == 16 && checkpoint.madr == 0x1010,
            "one diagnostic tick transfers one qword");
        cd.write32(0x1f8010f0, 0);
        const auto paused = cd.state(); cd.tick(ram);
        check(cd.state() == paused && ram == ram_checkpoint, "disabled DMA advanced");
        cd.restore(checkpoint);
        for (unsigned n = 1; n < 128; ++n) cd.tick(ram);
        const auto completed = cd.state();
        check(!completed.reading && completed.transferred == 2048 && completed.bcr == 32 &&
            completed.chcr == 0x40000200 && completed.irq == 2 && completed.error == 0,
            "DMA/read completion registers");
        check(std::equal(completed.media.begin() + 2048, completed.media.end(), ram.begin() + 0x1000),
            "exact original sector bytes");
        check(ram[0xfff] == 0xcc && ram[0x1800] == 0xcc, "DMA wrote outside destination");
        cd.restore(checkpoint); ram = ram_checkpoint;
        for (unsigned n = 1; n < 128; ++n) cd.tick(ram);
        check(cd.state() == completed, "in-flight CDVD replay");
        cd.write8(0x1f402008, 2);
        check(!cd.irq(), "CDVD IRQ W1C");
        auto invalid = cd.state(); invalid.transferred = 3;
        const auto before = cd.state();
        rejects([&] { cd.restore(invalid); });
        check(cd.state() == before, "invalid restore was not atomic");
        Cdvd other; other.mount(std::vector<std::uint8_t>(4096, 0));
        rejects([&] { other.restore(completed); });
        Cdvd empty;
        rejects([&] { empty.restore(completed); });
        cd.write32(0x1f8010b0, static_cast<std::uint32_t>(ram.size()) - 16);
        cd.write32(0x1f8010b4, 0x100020);
        cd.write32(0x1f8010b8, 0x41000200);
        command(cd, 0, 1);
        const auto ram_before = ram;
        cd.tick(ram);
        check(cd.state().stop && cd.state().transferred == 0 && ram == ram_before,
            "out-of-range DMA must stop before any writes");
        const auto stopped = cd.state(); cd.tick(ram);
        check(cd.state() == stopped, "CDVD fault not stable");
        Cdvd multi;
        multi.mount(media());
        dma(multi, 0x3000, 2);
        command(multi, 0, 2);
        for (unsigned n = 0; n < 9; ++n) multi.tick(ram);
        multi.write32(0x1f8010b8, 0);
        const auto canceled = multi.state();
        const auto canceled_ram = ram;
        multi.tick(ram);
        check(multi.state() == canceled && ram == canceled_ram,
            "cleared DMA start did not pause command");
        rejects([&] { multi.write32(0x1f8010b0, 0); });
        multi.write32(0x1f8010b8, 0x41000200);
        for (unsigned n = 9; n < 256; ++n) multi.tick(ram);
        check(!multi.state().reading && multi.state().transferred == 4096 &&
            multi.state().bcr == 32 && multi.state().madr == 0x4000 &&
            std::equal(multi.state().media.begin(), multi.state().media.end(), ram.begin() + 0x3000),
            "two-sector DMA pause/resume did not preserve exact stream");
        Cdvd mismatch;
        mismatch.mount(media());
        dma(mismatch, 0x5000);
        command(mismatch, 0, 2);
        const auto mismatch_ram = ram;
        mismatch.tick(ram);
        check(mismatch.state().stop && mismatch.state().transferred == 0 && ram == mismatch_ram,
            "mismatched DMA length transferred partial data");
        Cdvd overflow;
        for (unsigned n = 0; n < 11; ++n) overflow.write8(0x1f402005, 0);
        rejects([&] { overflow.write8(0x1f402005, 0); });
        std::cout << "CDVD tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
