#include "critterlink/system.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace critterlink;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class T> auto snap(const T& object) {
    using S = std::remove_cvref_t<decltype(object.state())>;
    return std::unique_ptr<S>(new S(object.state()));
}
template<class E = std::invalid_argument, class F> void rejects(F fn) {
    try { fn(); }
    catch (const E&) { return; }
    throw std::runtime_error("missing rejection");
}
void forward_channels() {
    for (unsigned flags : {0U, 0x40U, 0x80U, 0xc0U}) {
        for (unsigned base : {0x10009000U, 0x1000a000U}) {
            auto m = std::make_unique<Memory>();
            // Valid VIF NOPs / empty GIF tag. Bytes after QWC are deliberately invalid.
            m->write_quadword(0x1000, {0, 0});
            m->write_quadword(0x1010, {0xffffffffffffffffULL, 0xffffffffffffffffULL});
            m->write(0x1000e000, 4, 1);
            m->write(base + 0x10, 4, 0x1000);
            m->write(base + 0x20, 4, 1);
            m->write(base, 4, 0x101 | flags);
            const auto active = snap(*m);
            m->advance(2);
            check(!m->hardware().stop() && m->read(base, 4) == (1 | flags) &&
                  m->read(base + 0x10, 4) == 0x1010 && m->read(base + 0x20, 4) == 0,
                  "normal flags forwarded tags, overread or changed completion");
            const unsigned completion = base == 0x10009000 ? 2 : 4;
            check(m->read(0x1000e010, 4) == completion, "normal completion independent of TIE");
            const auto done = snap(*m);
            m->restore(*active);
            m->advance(2);
            check(*snap(*m) == *done, "normal flag active snapshot replay");
            m->restore(*done);
            m->write(0x1000e010, 4, completion);
            m->advance(2);
            check(m->read(0x1000e010, 4) == 0, "normal TIE generated extra completion");
            for (unsigned invalid : {0x109U, 0x111U, 0x201U, 0x10000101U}) {
                const auto before = snap(*m);
                rejects<MemoryFault>([&] { m->write(base, 4, invalid | flags); });
                check(*snap(*m) == *before, "normal unsupported bits partially launched");
            }
        }
    }
    auto m = std::make_unique<Memory>();
    // Normal TTE acceptance must not broaden GIF source-chain TTE support.
    rejects<MemoryFault>([&] { m->write(0x1000a000, 4, 0x145); });
}

void command(Memory& m, unsigned address, std::uint64_t value) {
    m.write_quadword(0x10006000, {0x1000000000008001ULL, 14});
    m.write_quadword(0x10006000, {value, address});
    m.advance(2);
}
void reverse_channel() {
    for (unsigned flags : {0U, 0x40U, 0x80U, 0xc0U}) {
        auto m = std::make_unique<Memory>();
        command(*m, 0x50, 1ULL << 48);
        command(*m, 0x51, 0);
        command(*m, 0x52, (1ULL << 32) | 32);
        command(*m, 0x53, 0);
        m->write_quadword(0x10006000, {0x0800000000008008ULL, 0});
        m->advance(1);
        for (unsigned n = 0; n < 32; n += 4) {
            m->write_quadword(0x10006000, {(n + 1) | (std::uint64_t{n + 2} << 32),
                                         (n + 3) | (std::uint64_t{n + 4} << 32)});
            m->advance(1);
        }
        command(*m, 0x50, 1ULL << 16);
        command(*m, 0x53, 1);
        m->write(0x10003c00, 4, 1U << 23);
        m->write(0x12001040, 8, 1);
        m->write(0x10009010, 4, 0x2000);
        m->write(0x10009020, 4, 8);
        m->write(0x10009000, 4, 0x100 | flags);
        m->write(0x1000e000, 4, 1);
        m->advance(10);
        check(!m->hardware().stop() && m->read(0x10009000, 4) == flags &&
              m->read(0x1000e010, 4) == 2, "reverse flags or completion retention");
        for (unsigned n = 0; n < 32; ++n) {
            check(m->read(0x2000 + n * 4, 4) == n + 1, "reverse flags changed pixel data");
        }
        const auto stopped = snap(*m);
        m->restore(*stopped);
        check(*snap(*m) == *stopped, "reverse stopped flags snapshot");
        m->write(0x10009020, 4, 7);
        rejects<MemoryFault>([&] { m->write(0x10009000, 4, 0x100 | flags); });
    }
}

void sif_channels() {
    for (unsigned flags : {0U, 0x40U, 0x80U, 0xc0U}) {
        auto sif = std::make_unique<Sif>();
        std::vector<std::uint8_t> ee(2048), iop(2048);
        for (unsigned n = 0; n < 128; ++n) {
            ee[0x100 + n] = static_cast<std::uint8_t>(n ^ 0xa5);
            iop[0x300 + n] = static_cast<std::uint8_t>(255 - n);
        }
        sif->iop_write32(0x1f801570, 0x8800);
        sif->iop_write32(0x1f801578, 1);
        sif->iop_write32(0x1f8010f4, 0x800000);
        sif->iop_write32(0x1f801574, 0xc0000);
        sif->ee_write32(0x1000c410, 0x100);
        sif->ee_write32(0x1000c420, 8);
        sif->ee_write32(0x1000c400, 0x101 | flags);
        sif->ee_write32(0x1000c010, 0x400);
        sif->ee_write32(0x1000c020, 8);
        sif->ee_write32(0x1000c000, 0x100 | flags);
        sif->iop_write32(0x1f801530, 0x200);
        sif->iop_write32(0x1f801534, 0x10020);
        sif->iop_write32(0x1f801538, 0x01000200);
        sif->iop_write32(0x1f801520, 0x300);
        sif->iop_write32(0x1f801524, 0x10020);
        sif->iop_write32(0x1f801528, 0x01000201);
        sif->tick(ee, iop, true);
        const auto partial = snap(*sif);
        const auto ee_partial = ee, iop_partial = iop;
        for (unsigned n = 0; n < 12; ++n) sif->tick(ee, iop, true);
        check(std::equal(ee.begin() + 0x100, ee.begin() + 0x180, iop.begin() + 0x200) &&
              std::equal(iop.begin() + 0x300, iop.begin() + 0x380, ee.begin() + 0x400),
              "SIF normal flags changed transferred bytes");
        check(sif->ee_read32(0x1000c400) == (1 | flags) &&
              sif->ee_read32(0x1000c000) == flags, "SIF stopped flags not retained");
        const auto done = snap(*sif);
        sif->restore(*partial);
        ee = ee_partial;
        iop = iop_partial;
        for (unsigned n = 0; n < 12; ++n) sif->tick(ee, iop, true);
        check(*snap(*sif) == *done, "SIF flag snapshot replay");
        sif->restore(*done);
        check(sif->take_ee_completions() == 0x60 && sif->take_ee_completions() == 0,
              "SIF TIE added events or suppressed QWC completion");
        for (unsigned base : {0x1000c000U, 0x1000c400U}) {
            const unsigned direction = base == 0x1000c400 ? 1 : 0;
            sif->ee_write32(base, flags | direction);
            check(sif->ee_read32(base) == (flags | direction), "SIF stopped configuration write");
            for (unsigned bits : {4U, 8U, 0x10U, 0x200U, 0x10000000U}) {
                const auto before = snap(*sif);
                rejects([&] { sif->ee_write32(base, 0x100 | flags | direction | bits); });
                check(*snap(*sif) == *before, "SIF unsupported mode partially launched");
            }
        }
    }
}

void sdk_normal_guest() {
    auto s = std::make_unique<System>();
    auto& m = s->memory();
    m.write_quadword(0x2000, {0x1000000000008001ULL, 14});
    m.write_quadword(0x2010, {0, 0x61});
    constexpr std::uint32_t code[] {
        0x3c081001, 0x34090001, 0xad09e000, 0x34092000, 0xad09a010,
        0x34090002, 0xad09a020, 0x34090181, 0xad09a000, 0x1000ffff, 0
    };
    for (unsigned n = 0; n < std::size(code); ++n) m.write(n * 4, 4, code[n]);
    check(s->run(24).budget_exhausted && !m.hardware().stop() &&
          m.read(0x1000a000, 4) == 0x81 && m.read(0x1000e010, 4) == 4 &&
          m.read(0x12001000, 8) == 2, "SDK normal-send guest did not deliver FINISH packet");
}
}
int main() {
    try {
        forward_channels();
        reverse_channel();
        sif_channels();
        sdk_normal_guest();
        std::cout << "Normal DMA flag tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
