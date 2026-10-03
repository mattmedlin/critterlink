# Milestone 4: dependency plan and first delivery

Parent: [#4](https://github.com/mattmedlin/critterlink/issues/4). **The umbrella
milestone remains open.** A rendered sprite, decoded block, or host controller
poll does not establish full PS2 hardware integration.

## Delivery plan

The first delivery is a bounded, testable foundation: schedule devices, expose
timer/INTC registers, move a normal GIF DMA packet from guest RAM, render its
sprite, and restore during the transfer. Separately expose strict pad/audio
primitives so later device work has independently tested building blocks.
Validate on every existing OS/configuration and preserve all earlier fixtures.

Follow the tracked issues in dependency order; each must meet its own criteria
before the umbrella's broader criteria are checked off:

| Issue | Deliverable | Dependencies | Current implementation |
| --- | --- | --- | --- |
| [#8](https://github.com/mattmedlin/critterlink/issues/8) | Scheduler, EE timers and INTC | #3 | Implemented diagnostic subset; explicit policies below |
| [#9](https://github.com/mattmedlin/critterlink/issues/9) | GIF normal DMA and reference sprite renderer | #8 | Implemented diagnostic subset |
| [#10](https://github.com/mattmedlin/critterlink/issues/10) | COP0 interrupt/exception dispatch and ERET | #8 | Implemented kernel diagnostic dispatch and ERET; see [interrupts.md](interrupts.md) |
| [#11](https://github.com/mattmedlin/critterlink/issues/11) | VIF and vector units | DMA channels/FIFOs beyond #9 | VIF1 normal DMA and diagnostic VU1 subset; see [vector.md](vector.md) |
| [#12](https://github.com/mattmedlin/critterlink/issues/12) | IOP and SIF communication | #8, #10, DMA extensions | Not implemented |
| [#13](https://github.com/mattmedlin/critterlink/issues/13) | Register-driven SPU2 audio | #12 | Filter-zero ADPCM primitive only; no voice engine or playback |
| [#14](https://github.com/mattmedlin/critterlink/issues/14) | SIO2 controllers, memory cards and disc | #12 | Host digital-pad poll primitive only; storage/disc absent |
| [#15](https://github.com/mattmedlin/critterlink/issues/15) | Guest-driven audiovisual/input restoration | #10–#14 | CPU/DMA/timer/graphics replay works; full integration remains open |

Issues #10/#11 add guest interrupt dispatch and a VIF1/VU1 diagnostic path.
Next is IOP/SIF (#12), with additional DMA and vector coverage tracked as needed. SPU2 and SIO2 should consume real IOP-side transactions before
their host primitives can count as integrated console devices. Memory-card and
disc diagnostics should use original synthetic media, never proprietary dumps.

## Host-neutral system and timing contract

`System` owns CPU, RAM/hardware, two digital pad endpoints, immutable recorded
input, and an input cursor. A successful CPU instruction or guest exception entry advances one logical
bus tick. This ratio is a **diagnostic scheduling policy**, not the real EE
clock ratio or instruction timing. Host stops advance no device time. On each
tick timers run first, then one VU1 pair, up to one VIF1 DMA qword, and one GIF
DMA qword. A stalled VIF word holds its channel without blocking timers or VU. Recorded pad input applies before the
CPU instruction at its timestamp; same-tick samples retain supplied order.
Analog input is rejected until supported. Direct pad polls use the serial
diagnostic endpoint, not SIO2 MMIO.

The scheduler has explicit absolute time, monotonically assigned event IDs,
stable equal-time ordering, and no host callbacks or pointers in its state.
The current hardware profile owns one recurring logical-bus event. Zero-budget
runs are no-ops. Time overflow is rejected before executing another instruction.
Unsupported graphics input or DMA source access stops device progression with
a diagnostic. A DMA-failing tick retains earlier timer work and earlier valid
qwords; it does not pretend the entire transfer was atomic.

`System::state/restore` covers CPU, complete RAM, scheduler queue/time, timer
prescalers/flags, DMA progress, partial VIF DMA qword, VIF upload, VU registers/memories/execution,
partial GIF payload and pending sprite vertex,
framebuffer, controller transaction state, input samples and cursor. Restore
validates into a replacement before changing live state. It is an in-memory
snapshot API, not a stable on-disk save-state format. Host file/media handles
and graphics/audio backends are not present. `Memory::clear` clears RAM only;
construct a fresh `System` to reset everything. Advance through `System::run`
when replaying input; directly advancing its mutable memory clock can invalidate
the input cursor and is rejected on the next run.

`--elf` now uses `System::run`, so loaded guest code can access the implemented
MMIO and progress devices. `Cpu::run` and `--demo` remain CPU-only stepping.
The original `Machine` input-clock API is retained for milestone 1 compatibility;
the coordinated path is `System`, which consumes the same `InputEvent` type.
An API call to `load_elf` resets CPU and loads its segments but preserves device
state, just as it preserves RAM outside segments. Start from a fresh system for
a cold diagnostic run.

## Timer, INTC and DMA profile

The bus recognizes the implemented EE registers at canonical physical addresses
and kernel aliases. Only aligned 32-bit data accesses are accepted. Other widths
and unknown registers fail explicitly; a misaligned access remains an address
error. Instructions cannot be fetched from MMIO.

| Registers | Implemented behavior |
| --- | --- |
| Timer n at `0x10000000 + n*0x800`, COUNT +0, MODE +0x10, COMP +0x20 | Four 16-bit counters, logical bus divisors 1/16/256, enable, zero-on-compare, compare/overflow flags and interrupt enables |
| INTC_STAT `0x1000f000` | Latched sources, write-one-clear; timers use bits 9–12 |
| INTC_MASK `0x1000f010` | Write-one-toggle; INT0 reflects enabled pending sources |
| D_CTRL `0x1000e000` | DMAE bit0; disabling pauses GIF DMA |
| D_STAT `0x1000e010` | Channels1/2 completion bits1/2 and masks17/18, bus-error bit15; low flags write-one-clear, high mask write-one-toggle |
| D1_CHCR `0x10009000`, MADR +0x10, QWC +0x20 | Normal RAM-to-VIF1 DMA with word-level stall/partial-qword state |
| D2_CHCR `0x1000a000` | Normal memory-to-GIF direction/start only; active restart or reprogramming rejected |
| D2_MADR `0x1000a010`, D2_QWC `0x1000a020` | Qword-aligned physical RAM source and 16-bit count; advance one qword per logical tick |

DMA completion clears STR and latches the relevant channel status. An out-of-RAM source
clears STR, sets channel/bus-error status and stops the diagnostic. A rejected
GIF qword leaves MADR/QWC at the offending qword and stops. Empty transfers
complete on the next enabled tick. A DMA boundary does not discard a partial
GIF packet. INT1 reflects enabled channel1/channel2 completion or a bus error.
VIF1 DMA latches the source qword before consuming words. On a stall the current
word, address and count remain pending. D_CTRL pauses delivery; VU execution
continues. Stopping CHCR discards that channel's pending qword while retaining
already-consumed VIF state; callers must provide the remaining stream when
restarting. This is a diagnostic transfer policy, not FIFO/bus cycle emulation.

Missing: timer gates, HBlank/VBlank clocks, SBUS HOLD, real-time frequencies,
other DMA channels, chain/interleave modes,
scratchpad DMA, FIFO stalls, arbitration and cycle-level bus timing.

Two timer details are explicitly **implementation policies awaiting hardware
validation**: a latched EQUF/OVFF suppresses additional events of the same kind
until the flag is acknowledged, and COUNT or clock/enable changes reset the
prescaler phase. Tests lock down this deterministic contract; they are not
silicon-conformance evidence. The primary manual describes register bits and
flag acknowledgement but does not resolve these timing/rearm details fully.

## Diagnostic and independent expectations

```sh
./build/critterlink --hardware-demo
```

The original literal instruction program in `src/hardware_demo.cpp` uses SW to
configure timer0, INTC and GIF DMA. Six qwords in RAM contain a real PACKED A+D
tag and GS register writes. The guest configures DMA; the CPU does not write the
expected framebuffer directly. The host supplies fixture bytes just as a loader
would. The sprite occupies x=2..4 and y=3..6: exactly 12 pixels, each
`0x80402010`; every other pixel remains zero. Timer0 and DMA interrupt lines are
both asserted by tick64; this fixture leaves CPU interrupts disabled. The
separate `--interrupt-demo` enables them and executes guest handlers.

At tick24 a snapshot contains one DMA qword remaining and a pending first sprite
vertex. A controller transaction has sent its first two bytes and latched Cross
pressed. An input release at tick26 must not change that in-flight packet.
After continuing/restoring/replaying, the complete state matches and the reply
is `ff 41 5a ff bf`. Separately, a filter-zero audio block decodes to initial PCM
samples `28672 -32768 4096 -4096` followed by zeroes. This is decoded sample data,
not timed SPU2 output. The CLI reports these separate pieces explicitly.

The `system`, `hardware`, `scheduler`, `graphics`, and `peripherals` test suites
cover replay, partial operations, invalid restores, timer/DMA ordering, fault
timing, exact pixels and serial/PCM values. Older ELF and CPU tests still run.
See [graphics](graphics.md), [peripherals](peripherals.md), and
[scheduling](scheduling.md) for subsystem-specific contracts and references.

Primary register reference: Sony *EE User's Manual*, version 6.0, INTC/timer/DMAC
chapters ([archived manual](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).
Register names/mode values were also cross-checked against the primary
[PS2SDK timer header](https://ps2dev.github.io/ps2sdk/timer_8h_source.html).
No emulator core or proprietary binary was imported.
