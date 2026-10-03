#include "critterlink/sio2.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace critterlink {
namespace {
constexpr std::uint32_t base = 0x1f808200;

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}

void valid_port(std::uint8_t port) {
    require(port < 2, "Invalid SIO2 port");
}

std::uint64_t image_hash(std::span<const std::uint8_t> image) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : image) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint8_t checksum(std::span<const std::uint8_t> bytes) {
    std::uint8_t result = 0;
    for (const auto byte : bytes) result ^= byte;
    return result;
}

void zeroes(std::span<const std::uint8_t> bytes) {
    require(std::all_of(bytes.begin(), bytes.end(), [](auto byte) { return byte == 0; }),
            "Unsupported nonzero SIO2 packet padding/actuator data");
}

std::uint32_t page_address(std::span<const std::uint8_t> packet) {
    std::uint32_t page = 0;
    for (unsigned i = 0; i < 4; ++i) page |= std::uint32_t{packet[2 + i]} << (i * 8);
    return page;
}

std::size_t descriptor_length(std::uint32_t descriptor) {
    const auto low = descriptor & 0xff;
    require(low == 0x40 || low == 0x41 || low == 0x72 || low == 0x73,
            "Unsupported SIO2 descriptor routing");
    require((descriptor & ~0x07fdffffU) == 0, "Unsupported SIO2 descriptor bits");
    const auto input_length = (descriptor >> 8) & 0x1ff;
    const auto output_length = (descriptor >> 18) & 0x1ff;
    require(input_length == output_length && input_length >= 4 &&
                input_length <= Sio2::fifo_capacity,
            "Unsupported SIO2 descriptor lengths");
    return input_length;
}

void valid_port_control(std::size_t index, std::uint32_t value) {
    if (value == 0) return;
    if ((index & 1) == 0) {
        require(value == 0xffc00505 || value == 0xff060505 || value == 0xff020405,
                "Unsupported SIO2 SEND1 timing value");
    } else {
        require(value == 0x2000a || value == 0x2012c || value == 0x5ffff,
                "Unsupported SIO2 SEND2 timing value");
    }
}

std::vector<std::uint8_t> pad_response(const Sio2State& state, unsigned port) {
    const auto& pad = state.pads[port];
    const auto& packet = state.input;
    const auto command = packet[1];
    require(packet[2] == 0, "Unsupported SIO2 pad header");
    require(command == 0x42 || command == 0x43 || command == 0x44,
            "Unsupported SIO2 pad command");
    const std::size_t length = pad.config || pad.analog ? 9 : 5;
    require(packet.size() == length, "Incorrect SIO2 pad packet length");
    std::vector<std::uint8_t> response(length, 0);
    response[0] = 0xff;
    response[1] = pad.config ? 0xf3 : (pad.analog ? 0x73 : 0x41);
    response[2] = 0x5a;
    if (command == 0x44) {
        require(pad.config && packet[3] <= 1 && (packet[4] == 0 || packet[4] == 3),
                "Unsupported SIO2 pad mode selection");
        zeroes(std::span(packet).subspan(5));
    } else {
        if (command == 0x43) {
            require(packet[3] == (pad.config ? 0 : 1), "Unsupported SIO2 config transition");
            zeroes(std::span(packet).subspan(4));
        } else {
            zeroes(std::span(packet).subspan(3));
        }
        if (!pad.config) {
            const auto buttons = static_cast<std::uint16_t>(~state.latched_input.buttons);
            response[3] = static_cast<std::uint8_t>(buttons);
            response[4] = static_cast<std::uint8_t>(buttons >> 8);
            if (pad.analog) {
                std::copy(state.latched_input.axes.begin(), state.latched_input.axes.end(),
                          response.begin() + 5);
            }
        }
    }
    return response;
}

std::vector<std::uint8_t> card_response(const Sio2State& state, unsigned port) {
    const auto& card = state.cards[port];
    const auto& packet = state.input;
    std::vector<std::uint8_t> response(packet.size(), 0);
    if (card.identity.empty()) {
        std::fill(response.begin(), response.end(), std::uint8_t{0xff});
        return response;
    }
    const auto command = packet[1];
    const auto bytes = std::span(packet);
    switch (command) {
    case 0x11:
    case 0x81:
    case 0x82:
        require(packet.size() == 4, "Incorrect SIO2 card short packet length");
        zeroes(bytes.subspan(2));
        if (command == 0x82) {
            require(card.operation == CardOperation::erase && card.writable,
                    "SIO2 card erase requires writable selected block");
            require(card.cursor % (Sio2::page_size * 16) == 0 &&
                        card.cursor <= card.data.size() - Sio2::page_size * 16,
                    "SIO2 card erase outside aligned block");
        }
        response[2] = 0x2b;
        response[3] = card.terminator;
        break;
    case 0x21:
    case 0x22:
    case 0x23: {
        require(packet.size() == 9, "Incorrect SIO2 card page packet length");
        require(checksum(bytes.subspan(2, 4)) == packet[6], "SIO2 card page checksum mismatch");
        zeroes(bytes.subspan(7));
        require(page_address(bytes) < card.data.size() / Sio2::page_size,
                "SIO2 card page outside mounted image");
        if (command != 0x23) require(card.writable, "SIO2 card is read-only");
        response[7] = 0x2b;
        response[8] = card.terminator;
        break;
    }
    case 0x27:
    case 0x28:
        require(packet.size() == 5, "Incorrect SIO2 terminator packet length");
        if (command == 0x27) {
            zeroes(bytes.subspan(3));
            response[3] = 0x2b;
            response[4] = card.terminator;
        } else {
            zeroes(bytes.subspan(2));
            response[2] = 0x2b;
            response[3] = card.terminator;
            response[4] = 0x55;
        }
        break;
    case 0x42:
    case 0x43: {
        const auto count = packet[2];
        require(count > 0 && count <= 128 && packet.size() == std::size_t{count} + 6,
                "Unsupported SIO2 card data length");
        require(card.cursor <= card.data.size() && count <= card.data.size() - card.cursor,
                "SIO2 card data outside mounted image");
        response[2] = 0x2b;
        response[3] = card.terminator;
        response[count + 5] = card.terminator;
        if (command == 0x42) {
            require(card.operation == CardOperation::write && card.writable,
                    "SIO2 card write requires writable selected page");
            require(checksum(bytes.subspan(3, count)) == packet[count + 3],
                    "SIO2 card write checksum mismatch");
            zeroes(bytes.subspan(count + 4));
            for (unsigned i = 0; i < count; ++i) {
                require((card.data[card.cursor + i] & packet[3 + i]) == packet[3 + i],
                        "SIO2 card programming requires erased bits");
            }
        } else {
            require(card.operation == CardOperation::read, "SIO2 card read requires selected page");
            zeroes(bytes.subspan(3));
            std::copy_n(card.data.begin() + card.cursor, count, response.begin() + 4);
            response[count + 4] = checksum(std::span(response).subspan(4, count));
        }
        break;
    }
    default:
        throw std::invalid_argument("Unsupported SIO2 memory-card command");
    }
    return response;
}

std::vector<std::uint8_t> make_response(const Sio2State& state) {
    const auto descriptor = state.send3[0];
    const auto length = descriptor_length(descriptor);
    require(state.input.size() == length, "SIO2 input does not match descriptor");
    const auto route = descriptor & 0xff;
    const auto port = descriptor & 1;
    if (route < 0x70) {
        require(state.input[0] == 1, "SIO2 pad descriptor/peripheral mismatch");
        return pad_response(state, port);
    }
    require(state.input[0] == 0x81, "SIO2 card descriptor/peripheral mismatch");
    return card_response(state, port);
}

void finish(Sio2State& state) {
    const auto port = state.send3[0] & 1;
    const auto command = state.input[1];
    if (state.input[0] == 1) {
        auto& pad = state.pads[port];
        if (command == 0x43) pad.config = state.input[3] != 0;
        if (command == 0x44) {
            pad.analog = state.input[3] != 0;
            pad.locked = state.input[4] == 3;
        }
        state.recv1 = 0x1100;
    } else {
        auto& card = state.cards[port];
        state.recv1 = card.identity.empty() ? 0x1d100 : 0x1100;
        if (!card.identity.empty()) {
            switch (command) {
            case 0x21:
            case 0x22:
            case 0x23:
                card.cursor = page_address(state.input) * Sio2::page_size;
                card.operation = command == 0x21 ? CardOperation::erase :
                    (command == 0x22 ? CardOperation::write : CardOperation::read);
                break;
            case 0x27:
                card.terminator = state.input[2];
                break;
            case 0x42:
                std::copy_n(state.input.begin() + 3, state.input[2], card.data.begin() + card.cursor);
                card.dirty = true;
                [[fallthrough]];
            case 0x43:
                card.cursor += state.input[2];
                break;
            case 0x81:
                card.operation = CardOperation::none;
                break;
            case 0x82:
                std::fill_n(card.data.begin() + card.cursor, Sio2::page_size * 16, std::uint8_t{0xff});
                card.dirty = true;
                card.operation = CardOperation::none;
                break;
            default:
                break;
            }
        }
    }
    state.active = false;
    state.control &= ~1U;
    state.interrupt_status = 1;
    state.byte_cursor = 0;
    state.input.clear();
    state.response.clear();
}

void clear_transfer(Sio2State& state) {
    state.input.clear();
    state.output.clear();
    state.response.clear();
    state.byte_cursor = 0;
    state.read_cursor = 0;
    state.active = false;
    state.recv1 = 0;
    state.interrupt_status = 0;
    state.stop.reset();
}
} // namespace

bool Sio2::address(std::uint32_t physical) noexcept {
    return physical >= base && physical <= base + 0x80;
}

std::uint8_t Sio2::read8(std::uint32_t physical) {
    require(physical == base + 0x64, "Unsupported SIO2 byte read");
    require(state_.read_cursor < state_.output.size(), "SIO2 output FIFO underflow");
    return state_.output[state_.read_cursor++];
}

void Sio2::write8(std::uint32_t physical, std::uint8_t value) {
    require(physical == base + 0x60, "Unsupported SIO2 byte write");
    require(!state_.active && !state_.stop, "SIO2 transfer busy or stopped");
    require(state_.input.size() < fifo_capacity, "SIO2 input FIFO overflow");
    state_.input.push_back(value);
}

std::uint32_t Sio2::read32(std::uint32_t physical) const {
    require(address(physical) && !(physical & 3), "Unsupported SIO2 word read");
    const auto offset = physical - base;
    if (offset < 0x40) return state_.send3[offset / 4];
    if (offset < 0x60) return state_.port_control[(offset - 0x40) / 4];
    switch (offset) {
    case 0x68: return state_.control;
    case 0x6c: return state_.recv1;
    case 0x70: return 0xf;
    case 0x74: return 0;
    case 0x80: return state_.interrupt_status;
    default: throw std::invalid_argument("Unsupported SIO2 word read");
    }
}

void Sio2::write32(std::uint32_t physical, std::uint32_t value) {
    require(address(physical) && !(physical & 3), "Unsupported SIO2 word write");
    const auto offset = physical - base;
    if (offset == 0x80) {
        require((value & ~1U) == 0, "Unsupported SIO2 interrupt acknowledgement");
        state_.interrupt_status &= ~value;
        return;
    }
    if (offset == 0x68) {
        require(value == 0 || value == 1 || value == 0xc || value == 0xd ||
                    value == 0x3bc || value == 0x3bd,
                "Unsupported SIO2 control value");
        if (!(value & 1)) {
            require((value & 0xc) || !state_.active, "Cannot cancel SIO2 without reset");
            if (value & 0xc) clear_transfer(state_);
            state_.control = value;
            return;
        }
        require(!state_.active && !state_.stop, "SIO2 transfer busy or stopped");
        require(state_.read_cursor == state_.output.size(), "SIO2 output must be drained before start");
        const auto length = descriptor_length(state_.send3[0]);
        require(state_.input.size() == length, "SIO2 input does not match descriptor");
        state_.latched_input = state_.pads[state_.send3[0] & 1].input;
        state_.output.clear();
        state_.read_cursor = 0;
        state_.response.clear();
        state_.active = true;
        state_.control = value;
        state_.recv1 = 0;
        state_.interrupt_status = 0;
        return;
    }
    require(!state_.active && !state_.stop, "SIO2 configuration while busy or stopped");
    if (offset < 0x40) {
        require(offset == 0 || value == 0, "SIO2 descriptor chains unsupported");
        if (value != 0) descriptor_length(value);
        state_.send3[offset / 4] = value;
        return;
    }
    if (offset < 0x60) {
        const auto index = (offset - 0x40) / 4;
        valid_port_control(index, value);
        state_.port_control[index] = value;
        return;
    }
    throw std::invalid_argument("Unsupported SIO2 word write");
}

void Sio2::tick() {
    if (!state_.active || state_.stop) return;
    try {
        if (state_.response.empty()) state_.response = make_response(state_);
        state_.output.push_back(state_.response[state_.byte_cursor++]);
        if (state_.byte_cursor == state_.response.size()) finish(state_);
    } catch (const std::invalid_argument& error) {
        state_.stop = error.what();
    }
}

void Sio2::set_controller(std::uint8_t port, const ControllerState& input) {
    valid_port(port);
    state_.pads[port].input = input;
}

void Sio2::mount_card(std::uint8_t port, std::string identity,
                     std::vector<std::uint8_t> image, bool writable) {
    valid_port(port);
    require(!state_.active, "Cannot change SIO2 media during a transfer");
    require(!identity.empty() && image.size() >= page_size * 16 &&
                image.size() <= max_card_size && image.size() % (page_size * 16) == 0,
            "Invalid SIO2 card identity or raw image size");
    Sio2CardState card;
    card.identity = std::move(identity);
    card.initial_hash = image_hash(image);
    card.data = std::move(image);
    card.writable = writable;
    state_.cards[port] = std::move(card);
}

void Sio2::eject_card(std::uint8_t port) {
    valid_port(port);
    require(!state_.active, "Cannot eject SIO2 media during a transfer");
    state_.cards[port] = Sio2CardState{};
}

std::vector<std::uint8_t> Sio2::export_card(std::uint8_t port) const {
    valid_port(port);
    require(!state_.cards[port].identity.empty(), "No SIO2 card mounted");
    return state_.cards[port].data;
}

void Sio2::restore(const Sio2State& saved) {
    require(saved.input.size() <= fifo_capacity && saved.output.size() <= fifo_capacity &&
                saved.response.size() <= fifo_capacity && saved.read_cursor <= saved.output.size(),
            "Invalid SIO2 FIFO snapshot");
    require(!saved.stop || !saved.stop->empty(), "Invalid SIO2 stop snapshot");
    require(saved.interrupt_status <= 1 &&
                (saved.recv1 == 0 || saved.recv1 == 0x1100 || saved.recv1 == 0x1d100),
            "Invalid SIO2 status snapshot");
    require(saved.control == 0 || saved.control == 1 || saved.control == 0xc ||
                saved.control == 0xd || saved.control == 0x3bc || saved.control == 0x3bd,
            "Invalid SIO2 control snapshot");
    require(std::all_of(saved.send3.begin() + 1, saved.send3.end(), [](auto n) { return n == 0; }),
            "Unsupported SIO2 snapshot descriptor chain");
    if (saved.send3[0]) descriptor_length(saved.send3[0]);
    for (std::size_t i = 0; i < saved.port_control.size(); ++i) {
        valid_port_control(i, saved.port_control[i]);
    }
    for (unsigned port = 0; port < 2; ++port) {
        const auto& card = saved.cards[port];
        const auto& current = state_.cards[port];
        if (card.identity.empty()) {
            require(card == Sio2CardState{}, "Invalid absent SIO2 card snapshot");
        } else {
            require(card.data.size() >= page_size * 16 && card.data.size() <= max_card_size &&
                        card.data.size() % (page_size * 16) == 0 && card.cursor <= card.data.size() &&
                        card.operation <= CardOperation::erase,
                    "Invalid SIO2 card snapshot");
            require(card.writable || !card.dirty, "Invalid read-only SIO2 card snapshot");
            if (!card.dirty) {
                require(image_hash(card.data) == card.initial_hash,
                        "SIO2 pristine media hash mismatch");
            }
            if (card.operation == CardOperation::erase) {
                require(card.cursor < card.data.size() && card.cursor % page_size == 0,
                        "Invalid SIO2 erase cursor snapshot");
            }
        }
        if (!current.identity.empty()) {
            require(card.identity == current.identity && card.initial_hash == current.initial_hash &&
                        card.data.size() == current.data.size() && card.writable == current.writable,
                    "SIO2 snapshot mounted media identity mismatch");
        }
    }
    if (saved.active) {
        require((saved.control & 1) && saved.recv1 == 0 && saved.interrupt_status == 0 &&
                    saved.input.size() == descriptor_length(saved.send3[0]),
                "Invalid active SIO2 snapshot");
        if (saved.response.empty()) {
            require(saved.byte_cursor == 0 && saved.output.empty(), "Invalid unstarted SIO2 snapshot");
        } else {
            require(!saved.stop && saved.byte_cursor < saved.response.size() &&
                        saved.byte_cursor == saved.output.size() &&
                        saved.response == make_response(saved) &&
                        std::equal(saved.output.begin(), saved.output.end(), saved.response.begin()),
                    "Invalid SIO2 response progress snapshot");
        }
    } else {
        require(!(saved.control & 1) && saved.byte_cursor == 0 && saved.response.empty(),
                "Invalid idle SIO2 snapshot");
    }
    auto replacement = saved;
    state_ = std::move(replacement);
}

} // namespace critterlink
