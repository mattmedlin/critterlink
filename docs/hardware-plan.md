# Milestone 4: subsystem integration plan

Parent: [#4](https://github.com/mattmedlin/critterlink/issues/4). **Reopened for full hardware
implementation.** A rendered sprite, decoded block, or host controller
poll does not establish full PS2 hardware integration. The current
[subsystem status matrix](subsystem-status.md) records implemented subsets,
assumptions, missing modes and targeted evidence.

## Remaining hardware implementation

The goal is a general-purpose PS2 emulator. Existing #8–#15 diagnostics remain
valid, but only cover subsets. Milestone #4 stays open until the expanded hardware
work and full-system acceptance criteria are met. LOTR is not an implementation
boundary, and multiplayer/network design is deferred until local emulation works.

| Issue | Remaining work |
| --- | --- |
| [#16](https://github.com/mattmedlin/critterlink/issues/16) | EE integer execution and memory operations |
| [#17](https://github.com/mattmedlin/critterlink/issues/17) | EE MMI, FPU and architectural control |
| [#18](https://github.com/mattmedlin/critterlink/issues/18) | Memory map, reset and BIOS boot |
| [#19](https://github.com/mattmedlin/critterlink/issues/19) | IOP CPU, interrupts, timers and DMA completion |
| [#20](https://github.com/mattmedlin/critterlink/issues/20) | EE DMA, SIF, VIF and GIF completion |
| [#21](https://github.com/mattmedlin/critterlink/issues/21) | VU0 and VU1 execution completion |
| [#22](https://github.com/mattmedlin/critterlink/issues/22) | GS memory, rasterization and scanout |
| [#23](https://github.com/mattmedlin/critterlink/issues/23) | SPU2 full device and audio timing |
| [#24](https://github.com/mattmedlin/critterlink/issues/24) | CDVD image-backed firmware-facing device |
| [#25](https://github.com/mattmedlin/critterlink/issues/25) | SIO2 controllers and persistent memory cards |
| [#26](https://github.com/mattmedlin/critterlink/issues/26) | IPU and remaining PS2 hardware inventory |
| [#27](https://github.com/mattmedlin/critterlink/issues/27) | Hardware timing and full-system conformance |

Start with EE/IOP execution and memory/reset foundations, then expand transfer,
vector, graphics, audio and I/O paths alongside integrated tests. Timing and
snapshot work accompany every subsystem rather than being postponed to the end.
IPU and optional-device inventory must expose omissions explicitly. Desktop UI,
host window/audio/controller backends and packaging belong to #6; hardware-facing
scanout, samples and controller protocols belong here.

The [memory/reset inventory](memory.md) records the primary-reference mapping
requirements and the planned ROM, translation, scratchpad and cache sequence.

Completion requires a complete implementation inventory, user-supplied firmware
boot through real implemented paths, varied redistributable homebrew, independent
hardware expectations, restoration during activity, and the full platform matrix.
No single demo, instruction family or game establishes completion. Commercial-game
compatibility remains a separate broad test program in #5 and may reveal more
hardware defects. Primary reference review and small regression fixtures precede
each hardware extension; unsupported operations must not silently succeed.

## Completed diagnostic foundation

Issues #8–#15 provide deterministic scheduling, timer/interrupt dispatch, normal
DMA, a sprite renderer, VIF1/VU1 integer diagnostics, IOP/SIF communication, limited
SPU2 audio, synthetic card/disc access and combined audiovisual/input restoration.
Their exact scope remains in the [subsystem matrix](subsystem-status.md), linked
contracts and [validation history](validation.md). This is progress toward the
expanded milestone, not full hardware implementation.

## Host-neutral system and timing contract

`System` owns CPU, RAM/hardware, two digital pad endpoints, immutable recorded
input, and an input cursor. A successful CPU instruction, guest exception entry or bus-stall retry advances one logical
bus tick. This ratio is a **diagnostic scheduling policy**, not the real EE
clock ratio or instruction timing. Host stops advance no device time. On each
tick timers run first, then one enabled IOP instruction, SIF endpoint transfers,
one SIO2 byte, one CDVD DMA qword, one SPU2 logical sample, one VU1 pair,
up to one VIF1 DMA qword, one GIF DMA enqueue and one GIF FIFO decode. A stalled VIF word holds its channel without blocking timers or VU. Recorded pad input applies before the
CPU instruction at its timestamp; same-tick samples retain supplied order.
Scheduled buttons and axes feed the SIO2 pads; each packet latches input when
the guest starts its transfer. Direct host pad polls remain a separate legacy
diagnostic endpoint.

The scheduler has explicit absolute time, monotonically assigned event IDs,
stable equal-time ordering, and no host callbacks or pointers in its state.
The current hardware profile owns one recurring logical-bus event. Zero-budget
runs are no-ops. Time overflow is rejected before executing another instruction.
Unsupported graphics input or DMA source access stops device progression with
a diagnostic. A DMA-failing tick retains earlier timer work and earlier valid
qwords; it does not pretend the entire transfer was atomic.

`System::state/restore` covers both CPUs, both complete RAMs, SIF registers/FIFOs,
SPU2 sound RAM, transfer progress, voices, envelopes and PCM signature,
SIO2 FIFOs/controller modes/raw cards/media identity, CDVD command/DMA/media identity,
scheduler queue/time, timer
prescalers/flags, DMA progress, partial VIF DMA qword, VIF upload, VU registers/memories/execution,
the shared GIF FIFO and pause flag, partial GIF payload and pending sprite vertex,
framebuffer, controller transaction state, input samples and cursor. Restore
validates into a replacement before changing live state. It is an in-memory
snapshot API, not a stable on-disk save-state format. A CDVD snapshot requires
the same disc already mounted,
and existing card mounts must match their saved identity. Card bytes are part
of the snapshot, so restoring rewinds completed diagnostic writes as well.
Host file/media handles and graphics/audio backends are not present.
`Memory::clear` clears RAM only;
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

## VIF graphics transport

[DIRECT/DIRECTHL and synchronization](vif-direct.md) feed a separate PATH2
queue. GIF ownership persists until packet EOP; VIF masking and queue waits
preserve ordering and replay. Forward CPU VIF input, PATH1 and intermittent
arbitration remain unfinished.

## Timer, INTC and DMA profile

The bus recognizes the implemented EE registers at canonical physical addresses
and kernel aliases. Register accesses require aligned 32-bit data; the GIF FIFO additionally accepts
128-bit stores. Other widths and unknown registers fail explicitly; a misaligned access remains an address
error. Instructions cannot be fetched from MMIO.

| Registers | Implemented behavior |
| --- | --- |
| Timer n at `0x10000000 + n*0x800`, COUNT +0, MODE +0x10, COMP +0x20 | Four 16-bit counters, logical bus divisors 1/16/256 or explicit HBlank clock, HBlank/VBlank gates, enable, zero-on-compare, compare/overflow flags and interrupt enables; HOLD +0x30 on timers 0/1 |
| INTC_STAT `0x1000f000` | Latched sources, write-one-clear; timers use bits 9–12 |
| INTC_MASK `0x1000f010` | Write-one-toggle; INT0 reflects enabled pending sources |
| D_CTRL `0x1000e000` | DMAE bit0; disabling pauses implemented EE DMA endpoints |
| D_STAT `0x1000e010` | Channels1/2/5/6 completion bits1/2/5/6 and masks17/18/21/22, bus-error bit15; low flags write-one-clear, high mask write-one-toggle |
| SIF0/1 channels at `0x1000c000` / `0x1000c400` | Normal EE receive/send endpoints; see [sif.md](sif.md) |
| D1_CHCR `0x10009000`, MADR +0x10, QWC +0x20 | Normal and RAM source-chain VIF1 DMA with upper64 tag transport and partial-word stalls; [contract](vif-dma-chain.md) |
| D2_CHCR `0x1000a000` | Normal and RAM source-chain GIF DMA, TAG/ASP/TIE; active restart or reprogramming rejected; [chain contract](gif-dma-chain.md) |
| GIF_CTRL `0x10003000`, GIF_STAT `0x10003020` | Queue reset/pause and modelled path status; see [GIF FIFO](gif-fifo.md) |
| GIF_FIFO `0x10006000–0x10006ff0` | Shared 16-qword CPU/DMA queue; 128-bit stores only, full queue stalls |
| D2_MADR `0x1000a010`, D2_QWC `0x1000a020` | Qword-aligned physical RAM source and 16-bit count; advance at most one qword per logical tick |
| D2_TADR `0x1000a030`, ASR0 `0x1000a040`, ASR1 `0x1000a050` | RAM tag address and two saved CALL return addresses |

Normal EE DMA retains TIE/TTE as inert flags, including SDK-style CHCR=0x181;
see [normal flag compatibility](dma-normal-flags.md). DMA completion clears STR
and latches the relevant channel status. An out-of-RAM source
clears STR, sets channel/bus-error status and stops the diagnostic. A rejected
GIF qword remains at the FIFO head and stops decoding; DMA may already have
advanced because its transfer completes when data enters the FIFO. Empty transfers
complete on the next enabled tick. A DMA boundary does not discard a partial
GIF packet. INT1 reflects enabled channel1/2/5/6 completion or a bus error.
VIF1 DMA latches the source qword before consuming words. On a stall the current
word, address and count remain pending. D_CTRL pauses delivery; VU execution
continues. Stopping CHCR discards that channel's pending qword while retaining
already-consumed VIF state; callers must provide the remaining stream when
restarting. This is a diagnostic transfer policy, not FIFO/bus cycle emulation.

Missing: GS-driven video clocks, independent timer phase/gate conformance, remaining SBUS sources/control modes, real-time frequencies,
other DMA channels, SIF chains, GIF tag forwarding, stall control/MFIFO/interleave modes,
scratchpad DMA, hardware-accurate FIFO capacities, arbitration and cycle-level
bus timing. Implemented VIF stalls and SIF bounded queues are documented diagnostic
behavior, not measurements of hardware buffering.

Two timer details are explicitly **implementation policies awaiting hardware
validation**: a latched EQUF/OVFF suppresses additional events of the same kind
until the flag is acknowledged, and COUNT or clock/enable changes reset the
prescaler phase. Tests lock down this deterministic contract; they are not
silicon-conformance evidence. The primary manual describes register bits and
flag acknowledgement but does not resolve these timing/rearm details fully.

### EE blank inputs and gated counters

`Hardware` and `Memory` expose `set_ee_hblank(bool)` and `set_ee_vblank(bool)`.
They update EE-only signal levels; the existing IOP-only inputs remain separate.
The GS does not yet generate these signals, and the input methods consume no
scheduler time. VBlank rising/falling edges latch INTC bits2/3 even when masked;
repeating the same level does not generate another event.

The manufacturer timer modes (EE User's Manual v6.0, pp34–37) are implemented:
GATE/GATS select the gate; mode0 counts while low; modes1/2/3 reset and start at
rising/falling/both edges, then free-run subject to CUE. Selecting external
HBlank clock disables an HBlank gate, while a VBlank gate still applies. ZRET,
compare/overflow flags and their existing acknowledgement behavior apply to
each admitted count tick.

The following details are **functional policies awaiting independent hardware
validation**, not measured timing:

- A rising HBlank edge supplies one tick for CLKS=3; bus ticks do not increment
  that mode. Gate changes are processed before the external tick.
- Clock/gate configuration changes arm edge-gate waiting; configuring while the
  level is already high still waits for a new selected edge. Flag-only, IRQ,
  ZRET and CUE writes preserve whether the gate has started.
- Gate pauses preserve divider phase. COUNT writes, clock/CUE changes and
  qualifying gate edges reset phase. Qualifying edges reset count and phase
  even when CUE is clear; CUE independently prevents counting.
- Gate resets alone do not compare or create timer interrupts. Explicit call
  order determines the order of separate blank signals at a logical instant.

Snapshots retain both EE levels, edge-gate waiting and divider phase. External
clock phase must be zero; waiting is valid only for an effective edge gate.
The `ee_timer_gates` suite checks all four counters, three bus divisors, both
gate signals, four gate modes, external clocks, IRQ flags, write policies,
invalid snapshots and original EE guest interrupt/replay paths. These tests
verify the documented model. They do not establish physical clock ratios,
scan timing, simultaneous signal ordering or complete timer conformance.

### SBUS capture and timer HOLD

`Hardware::set_sbus_interrupt_line(high)` accepts the physical active-low SBUS
external request level; Memory forwards the same input for System integration.
It combines with the IOP request so either contributor can hold the line low. A falling
edge copies COUNT0/1 into HOLD0/1 and latches INTC_STAT bit1 regardless of INTC_MASK.
A held-low request does not create another event, including after W1C
acknowledgement. A rising edge only rearms detection. A subsequent falling edge
recaptures counts even if the previous status bit remains set. This input does
not advance scheduler time or change timer count, phase, mode, or compare.

HOLD registers at 0x10000030 and 0x10000830 support aligned word reads/writes,
retaining low16 bits and reading upper16 as zero. Timers2/3 have no HOLD and
reject access; their placeholder snapshot HOLD fields must remain zero.
Snapshot state includes captured values, both source contributions and the last
combined physical request level. Restore validates their consistency without
generating a new edge.
The initial inactive-high level and zero HOLD values are deterministic reset
choices, not measured silicon reset values.

The manufacturer EE User's Manual v6.0 describes falling-edge SBUS detection in
the INTC table (printed p28), timer capture (p34), and writable HOLD (pp35,39).
`ee_sbus_hold` checks those register/event rules, both mask states, edge history,
invalid-state atomicity and a literal EE guest interrupt handler that reads both
captures, acknowledges SBUS and returns through ERET. Checkpoints before service
and after rearming reproduce full System state and EE traces. Host-supplied
request transitions exercise the external input. An original IOP guest also
generates the signal through its SBUS control register, then an EE guest handles
the resulting interrupt. Current SIF DMA completions are not automatically
treated as SBUS requests. Remaining source/control modes, physical synchronizer
latency and independently measured timing remain open.

The IOP word register at `0x1f801450` supports request bit1 only. Setting it asserts
the EE SBUS contributor; clearing it releases that contributor. Unsupported
bits and non-word accesses reject explicitly before modifying the request.
[PS2SDK sceSifIntrMain](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/iop/system/sifman/src/sifman.c#L512)
reads this register, sets bit1, then clears it; the
[IOP SBUS driver](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/common/sbus/src/iop_sbus.c#L34)
independently identifies the request and release. Each guest write updates the
source immediately within its logical execution tick. EE INTC acknowledgement
and SIF mailbox acknowledgement do not clear the IOP source request.

The initial zero control value and retained bit1 readback are functional model
choices consistent with those software sequences, not full register/reset
measurements. Other control bits, including cache/reset-related controls, and
the reverse EE-to-IOP SBUS request remain unsupported. The external input models
an independent contributor for testing/future devices, rather than overriding
an asserted IOP request.

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
