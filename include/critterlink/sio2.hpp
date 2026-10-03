#pragma once

#include "critterlink/machine.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace critterlink {

struct Sio2PadState {
    ControllerState input{};
    bool analog{};
    bool config{};
    bool locked{};
    bool operator==(const Sio2PadState&) const = default;
};

enum class CardOperation : std::uint8_t { none, read, write, erase };
struct Sio2CardState {
    std::string identity;
    std::uint64_t initial_hash{};
    std::vector<std::uint8_t> data;
    std::uint32_t cursor{};
    CardOperation operation{CardOperation::none};
    std::uint8_t terminator{0x55};
    bool writable{true};
    bool dirty{};
    bool operator==(const Sio2CardState&) const = default;
};

struct Sio2State {
    std::array<std::uint32_t, 16> send3{};
    std::array<std::uint32_t, 8> port_control{};
    std::uint32_t control{};
    std::uint32_t recv1{};
    std::uint32_t interrupt_status{};
    std::array<Sio2PadState, 2> pads{};
    std::array<Sio2CardState, 2> cards{};
    ControllerState latched_input{};
    std::vector<std::uint8_t> input;
    std::vector<std::uint8_t> response;
    std::vector<std::uint8_t> output;
    std::uint16_t byte_cursor{};
    std::uint16_t read_cursor{};
    bool active{};
    std::optional<std::string> stop;
    bool operator==(const Sio2State&) const = default;
};

class Sio2 {
public:
    static constexpr std::size_t fifo_capacity = 134;
    static constexpr std::size_t page_size = 528;
    static constexpr std::size_t max_card_size = page_size * 16384;
    static bool address(std::uint32_t physical) noexcept;
    std::uint8_t read8(std::uint32_t physical);
    void write8(std::uint32_t physical, std::uint8_t value);
    std::uint32_t read32(std::uint32_t physical) const;
    void write32(std::uint32_t physical, std::uint32_t value);
    void tick();
    void set_controller(std::uint8_t port, const ControllerState& input);
    void mount_card(std::uint8_t port, std::string identity,
                    std::vector<std::uint8_t> image, bool writable = true);
    void eject_card(std::uint8_t port);
    std::vector<std::uint8_t> export_card(std::uint8_t port) const;
    const Sio2State& state() const noexcept { return state_; }
    // Empty instances may adopt media. Existing mounts must match identity/hash.
    void restore(const Sio2State& state);
private:
    Sio2State state_;
};

} // namespace critterlink
