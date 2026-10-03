#include "critterlink/io_demo.hpp"

#include <algorithm>
#include <stdexcept>

namespace critterlink {
namespace {
void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}
constexpr std::uint32_t instruction(unsigned op, unsigned rs, unsigned rt, std::uint16_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) | imm;
}
struct Program {
    std::vector<std::uint32_t> words;
    std::uint64_t first_poll{};
    void constant(unsigned reg, std::uint32_t value) {
        words.push_back(instruction(15, 0, reg, static_cast<std::uint16_t>(value >> 16)));
        words.push_back(instruction(13, reg, reg, static_cast<std::uint16_t>(value)));
    }
    void write(unsigned width, std::uint16_t offset, std::uint32_t value) {
        constant(2, value);
        words.push_back(instruction(width == 1 ? 40 : 43, 1, 2, offset));
    }
    void poll(unsigned width, std::uint16_t offset, std::uint16_t mask) {
        words.push_back(instruction(width == 1 ? 36 : 35, 1, 3, offset));
        words.push_back(0); // IOP load delay.
        words.push_back(instruction(12, 3, 3, mask));
        words.push_back(instruction(4, 3, 0, 0xfffc));
        words.push_back(0); // Branch delay.
    }
    void read_byte(std::uint16_t offset, std::uint16_t destination) {
        words.push_back(instruction(36, 1, 3, offset));
        words.push_back(0);
        words.push_back(instruction(40, 0, 3, destination));
    }
    void serial(const std::vector<std::uint8_t>& packet, std::uint16_t destination, bool card = false) {
        constant(1, 0x1f808200);
        write(4, 0x68, 0x3bc);
        write(4, 0x40, card ? 0xff020405 : 0xffc00505);
        write(4, 0x44, card ? 0x0005ffff : 0x0002000a);
        const auto count = static_cast<std::uint32_t>(packet.size());
        write(4, 0, (count << 8) | (count << 18) | (card ? 0x72u : 0x40u));
        for (auto byte : packet) { write(1, 0x60, byte); }
        write(4, 0x68, 0x3bd);
        if (first_poll == 0) { first_poll = words.size(); }
        poll(4, 0x80, 1);
        for (std::size_t n = 0; n < packet.size(); ++n) {
            read_byte(0x64, static_cast<std::uint16_t>(destination + n));
        }
        write(4, 0x80, 1);
    }
};
Program program() {
    Program p;
    p.serial({1, 0x42, 0, 0, 0}, 0x6000);
    p.serial({1, 0x43, 0, 1, 0}, 0x6020);
    p.serial({1, 0x44, 0, 1, 3, 0, 0, 0, 0}, 0x6040);
    p.serial({1, 0x43, 0, 0, 0, 0, 0, 0, 0}, 0x6060);
    p.serial({1, 0x42, 0, 0, 0, 0, 0, 0, 0}, 0x6080);
    p.serial({0x81, 0x22, 1, 0, 0, 0, 1, 0, 0}, 0x6100, true);
    std::vector<std::uint8_t> write{0x81, 0x42, 16};
    std::uint8_t checksum = 0;
    for (unsigned n = 0; n < 16; ++n) {
        const auto byte = static_cast<std::uint8_t>(0xa0 + n);
        write.push_back(byte);
        checksum ^= byte;
    }
    write.insert(write.end(), {checksum, 0, 0});
    p.serial(write, 0x6120, true);
    p.serial({0x81, 0x81, 0, 0}, 0x6140, true);
    p.serial({0x81, 0x23, 1, 0, 0, 0, 1, 0, 0}, 0x6160, true);
    std::vector<std::uint8_t> read(22);
    read[0] = 0x81; read[1] = 0x43; read[2] = 16;
    p.serial(read, 0x6180, true);

    p.constant(1, 0x1f801000);
    p.write(4, 0xf0, 0x8000); // DMA3 enable.
    p.write(4, 0xb0, 0x7000);
    p.write(4, 0xb4, 0x00100020); // 16 blocks of 32 words = one sector.
    p.write(4, 0xb8, 0x41000200);
    p.constant(1, 0x1f402000);
    p.write(1, 6, 0x80); // HOWTO.
    for (auto byte : std::array<std::uint8_t, 11>{1,0,0,0,1,0,0,0,0,1,0}) {
        p.write(1, 5, byte);
    }
    p.write(1, 4, 6); // ReadCD.
    p.poll(1, 8, 2);
    p.read_byte(6, 0x61c0);
    p.read_byte(0xa, 0x61c1);
    p.write(1, 8, 2); // IRQ acknowledge.
    p.constant(2, 0x494f444e); // Original completion marker.
    p.words.push_back(instruction(43, 0, 2, 0x61d0));
    const auto idle = static_cast<std::uint32_t>(p.words.size());
    p.words.push_back(0x08000000 | idle);
    p.words.push_back(0);
    return p;
}
}

std::vector<InputEvent> io_demo_input() {
    return {{0, 0, {0x4000, {128,128,128,128}}},
            {program().first_poll + 1, 0, {8, {17,34,51,68}}}};
}

void prepare_io_demo(System& system) {
    system.memory().write(0, 4, 0x08000000);
    system.memory().write(4, 4, 0);
    const auto code = program().words;
    require(code.size() * 4 < 0x6000, "I/O fixture overlaps output RAM");
    for (std::size_t n = 0; n < code.size(); ++n) {
        system.memory().iop().write32(static_cast<std::uint32_t>(n * 4), code[n]);
    }
    system.memory().sio2().mount_card(0, "original-io-card", std::vector<std::uint8_t>(528 * 16, 0xff));
    std::vector<std::uint8_t> disc(4096);
    for (std::size_t n = 0; n < disc.size(); ++n) {
        disc[n] = static_cast<std::uint8_t>((n % 2048) * 13 + (n / 2048) * 31 + 7);
    }
    system.memory().cdvd().mount(std::move(disc));
    system.memory().iop().start();
}

IoDemoResult run_io_demo() {
    System system(io_demo_input());
    prepare_io_demo(system);
    require(system.run(program().first_poll + 2).budget_exhausted, "I/O setup stopped");
    const auto checkpoint = system.state();
    require(checkpoint.memory.hardware.sio2.active && checkpoint.memory.hardware.sio2.byte_cursor == 3,
            "I/O checkpoint is not mid controller packet");
    std::vector<InstructionTrace> first, replay;
    const auto finish = [](System& target, std::vector<InstructionTrace>& trace) {
        for (unsigned n = 0; n < 4096 && target.memory().iop().read32(0x61d0) != 0x494f444e; ++n) {
            require(target.run(1, &trace) == RunResult{1, true}, "Guest I/O diagnostic stopped");
        }
        require(target.memory().iop().read32(0x61d0) == 0x494f444e, "Guest I/O diagnostic timed out");
    };
    finish(system, first);
    const auto expected = system.state();
    system.restore(checkpoint);
    finish(system, replay);
    const auto& ram = system.memory().iop().state().ram;
    const std::array<std::uint8_t, 5> digital{0xff,0x41,0x5a,0xff,0xbf};
    const std::array<std::uint8_t, 9> analog{0xff,0x73,0x5a,0xf7,0xff,17,34,51,68};
    const bool digital_ok = std::equal(digital.begin(), digital.end(), ram.begin() + 0x6000);
    const bool analog_ok = std::equal(analog.begin(), analog.end(), ram.begin() + 0x6080);
    bool card_ok = true, disc_ok = ram[0x61c0] == 0 && ram[0x61c1] == 0x0a;
    for (unsigned n = 0; n < 16; ++n) {
        card_ok = card_ok && ram[0x6184 + n] == 0xa0 + n &&
            system.memory().sio2().state().cards[0].data[528 + n] == 0xa0 + n;
    }
    for (unsigned n = 0; n < 2048; ++n) {
        disc_ok = disc_ok && ram[0x7000 + n] == static_cast<std::uint8_t>(n * 13 + 38);
    }
    return {system.memory().hardware().now(), digital_ok, analog_ok, card_ok, disc_ok,
            system.state() == expected && first == replay};
}
} // namespace critterlink
