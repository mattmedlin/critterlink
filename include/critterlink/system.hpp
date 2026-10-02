#pragma once

#include "critterlink/cpu.hpp"
#include "critterlink/machine.hpp"
#include "critterlink/peripherals.hpp"

namespace critterlink {

struct SystemState {
    CpuState cpu;
    MemoryState memory;
    std::array<DigitalPadState, 2> pads;
    std::vector<InputEvent> input;
    std::size_t input_cursor{};
    bool operator==(const SystemState&) const = default;
};

// A retired instruction or exception entry advances one diagnostic bus tick.
class System {
public:
    explicit System(std::vector<InputEvent> input = {});
    Cpu& cpu() noexcept { return cpu_; }
    Memory& memory() noexcept { return memory_; }
    const Memory& memory() const noexcept { return memory_; }
    DigitalPad& pad(std::size_t port);
    RunResult run(std::uint64_t budget, std::vector<InstructionTrace>* trace = nullptr);
    SystemState state() const;
    void restore(const SystemState& state);
private:
    Cpu cpu_;
    Memory memory_;
    std::array<DigitalPad, 2> pads_;
    std::vector<InputEvent> input_;
    std::size_t input_cursor_{};
};

} // namespace critterlink
