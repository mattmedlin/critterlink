# IOP timers: functional clock and interrupt model

`IopTimers` supplies six counters to the IOP peripheral bus. This implements a
functional model; it does not establish physical timer conformance or complete
issue #19. Gate and boundary choices below remain provisional where the available
references do not settle behavior.

## Registers and clocks

| Timer | COUNT / MODE / TARGET physical addresses | Counter width | INTC bit | Optional external clock | Divider |
| --- | --- | --- | --- | --- | --- |
| 0 | `1f801100 / 104 / 108` | 16 | 4 | Pixel input | 1 |
| 1 | `1f801110 / 114 / 118` | 16 | 5 | HBlank entry | 1 |
| 2 | `1f801120 / 124 / 128` | 16 | 6 | None | 1 or 8 |
| 3 | `1f801480 / 484 / 488` | 32 | 14 | HBlank entry | 1 |
| 4 | `1f801490 / 494 / 498` | 32 | 15 | None | 1, 8, 16 or 256 |
| 5 | `1f8014a0 / 4a4 / 4a8` | 32 | 16 | None | 1, 8, 16 or 256 |

Word accesses are implemented for all registers. Low-halfword MODE accesses work
for every timer; low-halfword COUNT/TARGET accesses work for timers 0–2. Other
widths/offsets reject before mutation, including byte and upper-halfword accesses.
The SDK uses these halfword controls even on its 32-bit timers.

MODE configuration occupies bits 0–9 and 13–14: gate enable/selection, target
reset, compare/overflow IRQ enables, repeat, LEVL, external clock selection and
dividers. Bit 10 records armed/toggle state; bits 11/12 are sticky compare/overflow
indications. MODE reads return then clear both indications. MODE writes reset
count and divider phase, clear indications and arm bit 10. TARGET writes rearm
bit 10 when LEVL is clear. COUNT/TARGET are width-masked; writes alone do not
perform equality detection. Upper unused bits read zero; inapplicable configuration
bits are retained without changing that timer's capability.

## Explicit input and scheduling

`advance_sysclock` supplies IOP clocks. `advance_pixel` supplies timer-0 external
clocks. `set_hblank` and `set_vblank` supply blank levels; repeated writes of the
same level do not create edges. External selection affects only timers 0/1/3.
The API does not derive any of these clocks from instruction retirement.

The containing Hardware currently supplies one IOP sysclock before each IOP
execution boundary, including boundaries with a disabled IOP. This is the existing
diagnostic scheduler ratio, **not a measured EE-to-IOP frequency relationship**.
Hardware's explicit pixel/blank methods route the resulting timer requests but
advance no scheduler time. The GS diagnostic does not yet generate these inputs.
A real scanout clock, oscillator ratio and simultaneous video-edge scheduling
remain required integration work. Zero elapsed clock calls leave state unchanged.

Timer methods return a mask of qualified rising IRQ events. Hardware latches
these in I_STAT, independently of INTC masking, before CPU dispatch. No pulse
width is invented. Multiple events in one API call coalesce in I_STAT while the
timer's final count, flags, divider phase and toggle parity retain their progress.
Clearing MODE flags does not acknowledge I_STAT, and clearing I_STAT does not
rearm a one-shot timer. Guest code handles those controls separately.

## Provisional boundary and gate contract

The following choices make the current model deterministic and testable; passing
its tests does not resolve the listed hardware uncertainties.

- Reset uses count/target/phase zero and MODE `0400`. Full silicon reset values
  and peripheral reset sequencing are not established.
- Compare occurs when an input tick reaches TARGET. Overflow occurs on wrap to
  zero. TARGET=0 compares once per full wrap; compare and overflow on that tick
  form one eligible IRQ event, even when both enables are set.
- Bit 3 resets on reaching TARGET, independently of IRQ enables. A count initially
  equal to TARGET waits a complete counter wrap before the next compare. A count
  above a nonzero target wraps first, then resumes the target-reset period.
- Flags latch independently of IRQ enables. One-shot consumes armed bit 10 once.
  Repeating pulse mode stays armed. Repeating LEVL mode alternates bit 10 on
  eligible events, requesting an IRQ on its armed-to-unarmed transitions.
- COUNT and TARGET writes preserve divider phase; MODE writes reset it. Divider
  remainder persists across split advances and paused clocks. Hardware write/reset
  dead cycles and exact propagation latency are not modeled.
- Timers 0/1/3 use HBlank/VBlank/VBlank gates. Enabled gate modes 0–3 respectively
  pause inside blank, reset on entry while otherwise running, reset on entry and
  run only inside blank, or wait for the next entry then free-run. Programming
  wait mode while blank is already high waits for the next rising edge.
- Timers 2/4/5 have no blank input. Their enabled gate modes 0/3 stop counting;
  modes 1/2 run. Extending the PS1 timer-2 gate rule to timers 4/5 is provisional.
- HBlank entry resets the timer-0 gate before supplying one external clock to
  timers 1/3. VBlank transitions update gates 1/3. Call ordering defines coincident
  external transitions until a shared physical event schedule is implemented.

Elapsed-clock arithmetic skips complete periods without iterating once per tick
or event, supports `UINT64_MAX`, and retains odd/even IRQ-toggle parity. This is
an implementation property, not evidence for a particular physical boundary rule.

## State and validation

Snapshots contain count, target, MODE, divider remainder, gate-wait state and blank
levels. Restore validates widths, supported bits, remainder bounds and gate-wait
legality before replacing live state. Hardware/System snapshots include both the
timers and the interrupt controller's pending latch. External-clock replay must
supply the same subsequent explicit input transitions.

`iop_timers` exercises widths and side effects, one-shot rearming, pulse/toggle
sequences, zero targets, simultaneous flags, wrap and reset boundaries, all gate
modes, pixel/HBlank sources, divider phases, maximum elapsed clocks, invalid-state
atomicity and partitioned advances. These are literal functional-model expectations;
no captured PS2 timer-output oracle has yet been added.

Six original IOP guests program their respective timer, receive its interrupt,
read MODE twice through LHU to consume flags, acknowledge I_STAT, rearm TARGET
after the first service and return with JR/RFE. Each services exactly two
interrupts and then leaves the one-shot disarmed. Full System state and EE traces reproduce from checkpoints immediately
before compare and inside a delayed MFC0 in the handler. Explicit pixel-clock IRQ
routing has a separate Hardware replay test.

## Reference audit

- [ps2tek IOP timers](https://psi-rockin.github.io/ps2tek/#ioptimers) supplies the
  register layout, widths, mode fields, clock selection, divider settings and
  MODE/TARGET side effects. It calls bit 3 reset-on-interrupt, conflicting with
  the SDK's reset-on-target wording. Its toggle description does not settle all
  combinations or simultaneous events.
- [PS2SDK timrman.h](https://github.com/ps2dev/ps2sdk/blob/master/iop/system/timrman/include/timrman.h)
  and [timrman.c](https://github.com/ps2dev/ps2sdk/blob/master/iop/system/timrman/src/timrman.c)
  establish software access widths, counter capabilities, IRQ mapping and target
  rearming usage. The header describes bit 3 as reset-on-target. Its broad divider
  descriptions are restricted here by the timer capability table. SDK usage is
  not a full hardware characterization.
- [PSX-SPX timers](https://psx-spx.consoledev.net/timers/) informs the provisional
  gate model. PS1 behavior is not proof of PS2 timer behavior, particularly for
  the extra counters, reset cycles and overflow edge.

No external emulator implementation was imported. Independent PS2 tests for gate
modes, TARGET=0, maximum-versus-wrap IRQ timing, reset with IRQ disabled, coincident
flags, divider phase on writes and pulse duration remain necessary before claiming
hardware conformance or closing the subsystem.
