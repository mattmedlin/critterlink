#include "critterlink/sio2.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace critterlink;
constexpr std::uint32_t base = 0x1f808200;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function>
void rejects(Function action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Expected SIO2 rejection");
}

void start(Sio2& sio, const std::vector<std::uint8_t>& packet, unsigned port = 0) {
    sio.write32(base + 0x68, 0x3bc);
    const auto length = static_cast<std::uint32_t>(packet.size());
    const auto route = packet[0] == 1 ? 0x40U : 0x72U;
    sio.write32(base, (length << 8) | (length << 18) | route | port);
    for (auto byte : packet) sio.write8(base + 0x60, byte);
    sio.write32(base + 0x68, 0x3bd);
}

std::vector<std::uint8_t> drain(Sio2& sio) {
    std::vector<std::uint8_t> response;
    while (sio.state().read_cursor < sio.state().output.size()) {
        response.push_back(sio.read8(base + 0x64));
    }
    return response;
}

std::vector<std::uint8_t> transfer(Sio2& sio, const std::vector<std::uint8_t>& packet,
                                 unsigned port = 0) {
    start(sio, packet, port);
    for (std::size_t i = 0; i < packet.size(); ++i) sio.tick();
    check(!sio.state().stop && !sio.state().active, "SIO2 transfer completion");
    check(sio.read32(base + 0x80) == 1, "SIO2 interrupt completion");
    sio.write32(base + 0x80, 1);
    check(sio.read32(base + 0x80) == 0, "SIO2 interrupt acknowledgement");
    return drain(sio);
}

std::vector<std::uint8_t> select_page(std::uint8_t command, std::uint32_t page) {
    std::vector<std::uint8_t> packet{0x81, command, 0, 0, 0, 0, 0, 0, 0};
    for (unsigned i = 0; i < 4; ++i) {
        packet[2 + i] = static_cast<std::uint8_t>(page >> (i * 8));
        packet[6] ^= packet[2 + i];
    }
    return packet;
}

std::vector<std::uint8_t> write_packet() {
    std::vector<std::uint8_t> packet(22, 0);
    packet[0] = 0x81;
    packet[1] = 0x42;
    packet[2] = 16;
    for (unsigned i = 0; i < 16; ++i) {
        packet[3 + i] = static_cast<std::uint8_t>(0xa0 + i);
        packet[19] ^= packet[3 + i];
    }
    return packet;
}

void pad_modes_and_latching() {
    Sio2 sio;
    ControllerState first;
    first.buttons = 0x4000;
    sio.set_controller(0, first);
    start(sio, {1, 0x42, 0, 0, 0});
    sio.tick();
    ControllerState second;
    second.buttons = 8;
    second.axes = {17, 34, 51, 68};
    sio.set_controller(0, second);
    const auto middle = sio.state();
    for (unsigned i = 1; i < 5; ++i) sio.tick();
    const auto finished = sio.state();
    check(drain(sio) == std::vector<std::uint8_t>({0xff, 0x41, 0x5a, 0xff, 0xbf}),
          "Digital pad captures input at transfer start");
    sio.restore(middle);
    for (unsigned i = 1; i < 5; ++i) sio.tick();
    check(sio.state() == finished, "In-flight digital pad replay");
    drain(sio);
    transfer(sio, {1, 0x43, 0, 1, 0});
    check(sio.state().pads[0].config, "Guest enters configuration");
    auto response = transfer(sio, {1, 0x44, 0, 1, 3, 0, 0, 0, 0});
    check(response == std::vector<std::uint8_t>({0xff, 0xf3, 0x5a, 0, 0, 0, 0, 0, 0}),
          "Guest selects analog mode");
    transfer(sio, {1, 0x43, 0, 0, 0, 0, 0, 0, 0});
    check(transfer(sio, {1, 0x42, 0, 0, 0, 0, 0, 0, 0}) ==
              std::vector<std::uint8_t>({0xff, 0x73, 0x5a, 0xf7, 0xff, 17, 34, 51, 68}),
          "Analog guest polling and changed input");
    check(transfer(sio, {1, 0x42, 0, 0, 0}, 1) ==
              std::vector<std::uint8_t>({0xff, 0x41, 0x5a, 0xff, 0xff}),
          "Independent port 2 pad");
}

void card_persistence_and_replay() {
    Sio2 sio;
    const std::vector<std::uint8_t> blank(Sio2::page_size * 16, 0xff);
    sio.mount_card(0, "original synthetic card", blank);
    check(transfer(sio, {0x81, 0x11, 0, 0}) ==
              std::vector<std::uint8_t>({0, 0, 0x2b, 0x55}), "Card probe");
    check(transfer(sio, {0x81, 0x27, 0x5a, 0, 0})[4] == 0x55, "Old terminator reply");
    check(transfer(sio, {0x81, 0x28, 0, 0, 0}) ==
              std::vector<std::uint8_t>({0, 0, 0x2b, 0x5a, 0x55}), "New terminator read");
    transfer(sio, select_page(0x22, 1));
    const auto packet = write_packet();
    start(sio, packet);
    for (unsigned i = 0; i < 7; ++i) sio.tick();
    auto middle = sio.state();
    check(sio.export_card(0) == blank, "No partial card writes before command completion");
    rejects([&] { sio.eject_card(0); });
    for (std::size_t i = 7; i < packet.size(); ++i) sio.tick();
    const auto finished = sio.state();
    check(sio.state().cards[0].dirty, "Card dirty after completed write");
    sio.restore(middle);
    for (std::size_t i = 7; i < packet.size(); ++i) sio.tick();
    check(sio.state() == finished, "In-flight card programming replay");
    auto image = sio.export_card(0);
    check(std::equal(packet.begin() + 3, packet.begin() + 19, image.begin() + Sio2::page_size),
          "Guest bytes persisted in raw image");
    transfer(sio, {0x81, 0x81, 0, 0});
    sio.eject_card(0);
    sio.mount_card(0, "reloaded image", image);
    transfer(sio, select_page(0x23, 1));
    std::vector<std::uint8_t> read(22, 0);
    read[0] = 0x81;
    read[1] = 0x43;
    read[2] = 16;
    auto reply = transfer(sio, read);
    check(std::equal(reply.begin() + 4, reply.begin() + 20, packet.begin() + 3) && reply[20] == 0,
          "Reloaded card data and independent XOR checksum");
    Sio2 reloaded;
    reloaded.mount_card(0, "fresh imported image", image);
    transfer(reloaded, select_page(0x23, 1));
    check(transfer(reloaded, read) == reply, "Fresh device reads exported card image");
    const auto current = sio.state();
    rejects([&] { sio.restore(middle); });
    check(sio.state() == current, "Different mounted media restore rejected atomically");
    Sio2 adopted;
    adopted.restore(middle);
    check(adopted.state() == middle, "Fresh instance adopts snapshot media");
    transfer(sio, select_page(0x21, 0));
    transfer(sio, {0x81, 0x82, 0, 0});
    check(sio.export_card(0) == blank, "Block erase persistence");
}

void failures() {
    Sio2 sio;
    check(transfer(sio, {0x81, 0x11, 0, 0}) == std::vector<std::uint8_t>(4, 0xff) &&
              sio.state().recv1 == 0x1d100, "Missing card response/status");
    sio.mount_card(0, "read only", std::vector<std::uint8_t>(Sio2::page_size * 16, 0xff), false);
    start(sio, select_page(0x22, 1));
    sio.tick();
    check(sio.state().stop && sio.state().cards[0].cursor == 0, "Read-only card error");
    auto stopped = sio.state();
    sio.tick();
    check(sio.state() == stopped, "SIO2 error is sticky");
    sio.restore(stopped);
    sio.write32(base + 0x68, 0xc);
    sio.eject_card(0);
    sio.mount_card(0, "writable", std::vector<std::uint8_t>(Sio2::page_size * 16, 0xff));
    auto bad_page = select_page(0x22, 1);
    bad_page[6] ^= 1;
    start(sio, bad_page);
    sio.tick();
    check(sio.state().stop && sio.state().cards[0].operation == CardOperation::none,
          "Card address checksum rejects without media mutation");
    start(sio, select_page(0x23, 16));
    sio.tick();
    check(sio.state().stop.has_value(), "Out-of-range card page rejects");
    transfer(sio, select_page(0x22, 1));
    auto bad_write = write_packet();
    bad_write[19] = 1;
    const auto unmodified = sio.export_card(0);
    start(sio, bad_write);
    sio.tick();
    check(sio.state().stop && sio.export_card(0) == unmodified,
          "Bad write checksum preserves image");
    transfer(sio, select_page(0x22, 1));
    transfer(sio, write_packet());
    transfer(sio, select_page(0x22, 1));
    auto illegal_program = write_packet();
    std::fill(illegal_program.begin() + 3, illegal_program.begin() + 19, std::uint8_t{0xff});
    const auto programmed = sio.export_card(0);
    start(sio, illegal_program);
    sio.tick();
    check(sio.state().stop && sio.export_card(0) == programmed,
          "NAND zero-to-one programming requires erase");
    start(sio, {1, 0x99, 0, 0, 0});
    sio.tick();
    check(sio.state().stop.has_value(), "Unsupported pad command explicit stop");
    sio.write32(base + 0x68, 0xc);
    auto before = sio.state();
    rejects([&] { sio.read8(base + 0x64); });
    rejects([&] { sio.read32(base + 0x64); });
    rejects([&] { sio.write32(base + 4, 1); });
    check(sio.state() == before, "Invalid register access is transactional");
    for (std::size_t i = 0; i < Sio2::fifo_capacity; ++i) sio.write8(base + 0x60, 0);
    before = sio.state();
    rejects([&] { sio.write8(base + 0x60, 0); });
    check(sio.state() == before, "Bounded FIFO overflow transactional");
}

void malformed_snapshots() {
    Sio2 sio;
    start(sio, {1, 0x42, 0, 0, 0});
    sio.tick();
    const auto original = sio.state();
    auto malformed = original;
    malformed.response[1] = 0;
    rejects([&] { sio.restore(malformed); });
    malformed = original;
    malformed.byte_cursor = 99;
    rejects([&] { sio.restore(malformed); });
    malformed = original;
    malformed.read_cursor = 2;
    rejects([&] { sio.restore(malformed); });
    check(sio.state() == original, "Malformed snapshots preserve live state");
}
} // namespace

int main() {
    try {
        pad_modes_and_latching();
        card_persistence_and_replay();
        failures();
        malformed_snapshots();
        std::cout << "SIO2 diagnostic tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
