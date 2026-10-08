#pragma once

#include "critterlink/graphics.hpp"
#include "critterlink/scheduler.hpp"
#include "critterlink/vector.hpp"
#include "critterlink/iop.hpp"
#include "critterlink/iop_intc.hpp"
#include "critterlink/iop_timers.hpp"
#include "critterlink/sif.hpp"
#include "critterlink/spu.hpp"
#include "critterlink/sio2.hpp"
#include "critterlink/cdvd.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace critterlink {

struct TimerState {
    std::uint16_t count{}, compare{}, mode{}, phase{}, hold{};
    bool gate_wait{};
    bool operator==(const TimerState&) const = default;
};
enum class GifChainPhase { idle, tag, payload };
struct GifDmaState {
    std::uint32_t control{}, status{}, chcr{}, address{}, qwords{};
    std::uint32_t tag_address{};
    std::array<std::uint32_t, 2> asr{};
    GifChainPhase phase{GifChainPhase::idle};
    std::uint32_t next_tag{};
    bool packet_end{}, packet_irq{};
    bool operator==(const GifDmaState&) const = default;
};
enum class VifChainPhase { idle, tag, tag_words, payload };
struct VifDmaState {
    std::uint32_t chcr{}, address{}, qwords{};
    // Legacy transport fields remain canonical zero; accepted input lives in the shared FIFO.
    std::array<std::uint32_t, 4> pending{};
    std::uint8_t cursor{};
    bool loaded{};
    std::uint32_t tag_address{};
    std::array<std::uint32_t, 2> asr{};
    VifChainPhase phase{VifChainPhase::idle};
    std::uint32_t next_tag{};
    bool packet_end{}, packet_irq{};
    std::array<std::uint32_t, 2> tag_words{};
    unsigned tag_cursor{};
    bool operator==(const VifDmaState&) const = default;
};
struct GifFifoState {
    std::array<std::array<std::uint32_t, 4>, 16> words{};
    std::uint8_t head{}, count{};
    bool paused{};
    bool operator==(const GifFifoState&) const = default;
};
struct VifReadbackFifoState {
    std::array<std::array<std::uint32_t, 4>, 16> words{};
    std::uint8_t head{}, count{};
    bool operator==(const VifReadbackFifoState&) const = default;
};
enum class VifWait { none, vu, gif };
struct VifTransportState {
    std::uint32_t direct_remaining{};
    std::array<std::uint32_t, 4> words{};
    std::uint8_t lane{};
    VifWait wait{VifWait::none};
    bool operator==(const VifTransportState&) const = default;
};
enum class VifInputSource { cpu, dma, tte };
struct VifInputEntry {
    std::array<std::uint32_t, 4> words{};
    std::uint32_t address{};
    std::uint8_t cursor{};
    VifInputSource source{VifInputSource::cpu};
    bool operator==(const VifInputEntry&) const = default;
};
struct VifInputFifoState {
    std::array<VifInputEntry, 16> entries{};
    std::uint8_t head{}, count{};
    bool operator==(const VifInputFifoState&) const = default;
};
struct HardwareState {
    SchedulerState scheduler;
    std::array<TimerState, 4> timers{};
    std::uint32_t interrupt_status{}, interrupt_mask{};
    GifDmaState dma;
    GraphicsState graphics;
    std::optional<std::string> stop;
    VifDmaState vif_dma;
    VectorState vector;
    IopState iop;
    SifState sif;
    SpuState spu;
    Sio2State sio2;
    CdvdState cdvd;
    GifFifoState gif_fifo;
    IopIntcState iop_intc;
    IopTimersState iop_timers;
    bool sbus_interrupt_high{true};
    bool sbus_external_high{true};
    std::uint32_t iop_sbus_control{};
    bool ee_hblank{}, ee_vblank{};
    bool gs_interrupt_high{true};
    VifReadbackFifoState vif_readback_fifo;
    bool vif_fdr{}, gs_busdir{};
    GifFifoState gif_path2_fifo;
    std::uint8_t gif_owner{};
    bool path3_masked{};
    VifTransportState vif_transport;
    VifInputFifoState vif_input_fifo;
    bool operator==(const HardwareState&) const = default;
};

// Diagnostic logical-bus clock; CPU dispatches the resulting interrupt lines.
class Hardware {
public:
    Hardware();
    std::uint32_t read(std::uint32_t physical_address) const;
    void write(std::uint32_t physical_address, std::uint32_t value);
    // Canonical GS privileged ports; Memory enforces aligned 64-bit access.
    std::uint64_t read_gs(std::uint32_t address) const;
    void write_gs(std::uint32_t address, std::uint64_t value);
    // False means backpressure: no part of the qword was accepted.
    bool write_quadword(std::uint32_t physical_address, std::array<std::uint32_t, 4> words);
    // Empty optional means the supported readback is still producing data.
    std::optional<std::array<std::uint32_t, 4>> read_quadword(std::uint32_t physical_address);
    void advance(std::uint64_t ticks, std::span<std::uint8_t> ram, std::span<const std::uint8_t> boot_rom = {});
    // Explicit video-clock inputs; current GS diagnostic does not generate them.
    void advance_iop_pixel_clock(std::uint64_t ticks);
    void set_iop_hblank(bool active);
    void set_iop_vblank(bool active);
    // EE-only explicit blank edges; no scan timing or IOP input is synthesized.
    void set_ee_hblank(bool active) noexcept;
    void set_ee_vblank(bool active) noexcept;
    // External active-low SBUS contributor, combined with the IOP request.
    void set_sbus_interrupt_line(bool high) noexcept;
    std::uint32_t iop_sbus_control() const noexcept { return iop_sbus_control_; }
    void write_iop_sbus_control(std::uint32_t value);
    HardwareState state() const;
    void restore(const HardwareState& state);
    bool int0() const noexcept;
    bool int1() const noexcept;
    std::uint64_t now() const noexcept { return scheduler_.state().now; }
    const std::optional<std::string>& stop() const noexcept { return stop_; }
    const VectorState& vector() const noexcept { return vector_.state(); }
    Iop& iop() noexcept { return iop_; }
    const Iop& iop() const noexcept { return iop_; }
    const IopTimersState& iop_timers() const noexcept { return iop_timers_.state(); }
    const SifState& sif() const noexcept { return sif_.state(); }
    const SpuState& spu() const noexcept { return spu_.state(); }
    Sio2& sio2() noexcept { return sio2_; }
    const Sio2& sio2() const noexcept { return sio2_; }
    Cdvd& cdvd() noexcept { return cdvd_; }
    const Cdvd& cdvd() const noexcept { return cdvd_; }
    const GraphicsState& graphics() const noexcept { return graphics_.state(); }

private:
    void sample_iop_interrupts();
    void sample_sbus_interrupt() noexcept;
    void sample_gs_interrupt() noexcept;
    void tick_timers(bool external_clock = false);
    void tick_vif_dma(std::span<std::uint8_t> ram);
    void tick_readback();
    std::array<std::uint32_t, 4> pop_readback();
    void tick_dma(std::span<const std::uint8_t> ram);
    void tick_gif();
    void tick_vif_input();
    bool enqueue_vif(VifInputEntry entry);
    bool submit_vif_word(std::uint32_t word, unsigned physical_word, bool tag_word);
    bool enqueue_gif(std::array<std::uint32_t, 4> words);
    Scheduler scheduler_;
    std::array<TimerState, 4> timers_{};
    bool sbus_interrupt_high_{true}, sbus_external_high_{true};
    std::uint32_t iop_sbus_control_{};
    bool ee_hblank_{}, ee_vblank_{};
    bool gs_interrupt_high_{true};
    std::uint32_t interrupt_status_{}, interrupt_mask_{};
    GifDmaState dma_;
    Graphics graphics_;
    VifDmaState vif_dma_;
    VectorUnit vector_;
    Iop iop_;
    IopIntc iop_intc_;
    IopTimers iop_timers_;
    Sif sif_;
    Spu spu_;
    Sio2 sio2_;
    Cdvd cdvd_;
    GifFifoState gif_fifo_;
    VifReadbackFifoState vif_readback_fifo_;
    bool vif_fdr_{}, gs_busdir_{};
    GifFifoState gif_path2_fifo_;
    std::uint8_t gif_owner_{};
    bool path3_masked_{};
    VifTransportState vif_transport_;
    VifInputFifoState vif_input_fifo_;
    std::optional<std::string> stop_;
};

} // namespace critterlink
