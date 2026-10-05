# Milestone 1 validation

Verified locally on October 1, 2026:

- Native macOS ARM64 host, Apple Clang 15.0.0, CMake 4.4.3.
- Clean Debug and Release CMake configurations and builds completed with
  warnings treated as errors.
- Both CTest suites (`core`, `cli_contract`) passed in both configurations.
- `file` identified both CLI executables as `Mach-O 64-bit executable arm64`.
- `critterlink --ticks 1000` printed tick 1000 and exited successfully.

CMake was installed into `/tmp/critterlink-build-tools` for this verification;
no build tool or downloaded dependency is committed to the repository. To use
that temporary installation while it exists, substitute
`/tmp/critterlink-build-tools/cmake/data/bin/cmake` and
`/tmp/critterlink-build-tools/cmake/data/bin/ctest` in the README commands.

Core checks cover neutral state, zero advancement, input boundaries, both
controller ports, equal-tick order, ownership of replay input, reset and replay,
chunk-independent results, invalid ports, unordered input, and overflow without
state or playback-cursor mutation. CLI checks cover help, default invocation,
valid tick counts including the 64-bit maximum, malformed/empty/overflowing
counts, missing values, unknown flags, extra arguments, output streams, and
exit codes. CLI test subprocesses have timeouts to catch unexpected hangs.

Remote verification also passed on October 1, 2026 (America/New_York), for
commit `1f2be81291a4542cc11fc9232b5b130576f60346`:

- Windows x86-64, Linux x86-64, macOS ARM64, and macOS x86-64.
- Debug and Release on every platform: all eight jobs passed architecture
  checks, fresh-checkout configuration, compilation, and both CTest suites.
- [Successful GitHub Actions run](https://github.com/mattmedlin/critterlink/actions/runs/36946575644).

This satisfies the milestone's cross-platform build/test acceptance criterion.
No PS2 software was run, and no hardware emulation or game compatibility was
verified.

## Milestone 2 local validation

On October 1, 2026 (America/New_York), the scalar CPU slice and RAM bus passed
the `core`, `cpu_memory`, and `cli_contract` suites on native Apple Silicon with
Apple Clang 15 and CMake 4.4.3 in Debug and Release. A separate Debug build with
`-fsanitize=address,undefined -fno-omit-frame-pointer` also passed all suites.
The demo produced the independently specified results in
[cpu-coverage.md](cpu-coverage.md). Tests include arithmetic boundaries, delay
slots, memory aliases/endianness/alignment, exception observations, deterministic
replay, unsupported-opcode diagnostics, and CLI exit behavior.

Remote verification passed for commit `32729f6` in
[GitHub Actions run 36949045430](https://github.com/mattmedlin/critterlink/actions/runs/36949045430):
all eight Debug/Release jobs on Windows x86-64, Linux x86-64, macOS ARM64,
and macOS x86-64 configured, built, and passed all three test suites.
The first Windows build exposed a byte-fill type-conversion warning; using
an explicitly byte-typed fill value resolved it without disabling warnings.
The final code also passed the local address/undefined-behavior sanitizer run.

## Milestone 3 local validation

On October 1, 2026 (America/New_York), native Apple Silicon Debug, Release,
and address/undefined-behavior sanitizer builds passed all six suites: `core`,
`cpu_memory`, `elf_loader`, `elf_cli`, `fixture_reproducible`, and `cli_contract`.
The homebrew ELF produced the five expected signature words documented in
[homebrew.md](homebrew.md). The loader suite rejects every truncated prefix
and malformed-header/segment cases while checking that the full RAM image and
CPU state remain unchanged. The fixture rebuild is checked against a fixed
SHA-256 value so all supported hosts must execute identical guest bytes.

Remote verification passed for commit `c7af045` in
[GitHub Actions run 36950746904](https://github.com/mattmedlin/critterlink/actions/runs/36950746904):
all eight Debug/Release jobs on Windows x86-64, Linux x86-64, macOS ARM64,
and macOS x86-64 passed all six suites, including fixture hash and signature
checks. The first Windows build caught a test-data narrowing warning; replacing
the forwarding pair constructor with byte-typed aggregate initialization fixed
it without relaxing compiler warnings. The affected loader suite also passed
again with address/undefined-behavior sanitizers after that test-only fix.

## Milestone 4 first-delivery validation

On October 2, 2026 (America/New_York), native Apple Silicon Debug, Release,
and address/undefined-behavior sanitizer builds passed all eleven suites.
The five added suites cover scheduling, timers/INTC/GIF DMA, strict GIF sprite
rendering, peripheral primitives, and coordinated system restoration. Tests
check exact pixels and PCM samples, interrupt flags, unsupported operations,
time overflow, atomic rejection of invalid snapshots, and replay from partial
DMA, GIF, and pad transactions. All earlier CPU/ELF/CLI fixtures still pass.

`--hardware-demo` reports 64 logical ticks, 12 sprite pixels, both interrupt
lines asserted, identical replay, pad bytes `ff 41 5a ff bf`, and initial PCM
samples `28672 -32768 4096 -4096`. These validate the explicitly bounded
[hardware profile](hardware-plan.md), not complete PS2 hardware, calibrated
timing, guest interrupt handling, IOP/SIO2, or register-driven SPU2 audio.

Remote verification passed for commit `d4ae4a1` in
[GitHub Actions run 37034073971](https://github.com/mattmedlin/critterlink/actions/runs/37034073971):
all eight Debug/Release jobs on Windows x86-64, Linux x86-64, macOS ARM64,
and macOS x86-64 built with warnings treated as errors and passed all eleven
suites. This completes the scoped #8/#9 delivery; umbrella #4 remains open.

## COP0 dispatch (#10) local validation

On October 2, 2026 (America/New_York), native Apple Silicon Debug, Release,
and address/undefined-behavior sanitizer builds passed all thirteen suites.
New COP0 and interrupt suites cover Status masks, pending/disabled/simultaneous
interrupts, nested EXL exceptions, EPC/BD preservation, BEV vectors, ERET's ERL
priority, strict unsupported encodings, and bounded recursive handler faults.
The final invalid-Status snapshot atomicity assertion also passed separately
under sanitizers after the complete run.

The original `--interrupt-demo` guest acknowledges timer and DMA sources and
returns with ERET: 128 logical ticks, one service per source, 12 sprite pixels,
both sources acknowledged, and identical complete state/trace after restoring
inside its handler. The prior CPU, ELF, hardware and CLI fixtures still pass.
These results validate the [documented kernel subset](interrupts.md), not full
COP0, TLB, BIOS execution, privilege enforcement or cycle-accurate behavior.

Remote verification passed for `80be1b8` in
[GitHub Actions run 37068858723](https://github.com/mattmedlin/critterlink/actions/runs/37068858723):
all eight Debug/Release jobs on Windows x86-64, Linux x86-64, macOS ARM64,
and macOS x86-64 built and passed all thirteen suites. The initial Windows
build caught signed/unsigned comparisons inside optional-value test assertions;
unsigned literals fixed those warnings without changing emulator behavior or
compiler settings. The affected suite passed again under sanitizers.

## VIF1/VU1 diagnostic (#11) local validation

On October 2, 2026 (America/New_York), native Apple Silicon Debug and Release
passed all sixteen suites. After review caught MPG's 64-bit payload alignment
requirement, the DMA path gained explicit rejection and the original fixture
gained a leading NOP; all affected suites passed again in both configurations.
The final complete address/undefined-behavior sanitizer run passed all sixteen
suites. No earlier CPU, ELF, graphics or interrupt fixture regressed.

New tests cover partial MPG/UNPACK state, VIF stalls, E termination delay,
masked vector transfers, integer-immediate high bits and wrap, source bounds,
simultaneous GIF/VIF DMA completion, independent interrupt acknowledgement,
DMA pause/cancellation, and atomic rejection of unsupported words/snapshots.
Full system replay starts both mid-upload and mid-VU/FLUSHE stall. The original
`--vector-demo` guest reports 88 ticks, output `7,9,11,13`, completed DMA and
identical replay. See [vector.md](vector.md) for the limited instruction set and
logical timing policy; floating-point and pipeline conformance are not claimed.

Remote verification passed for `811d0f9` in
[GitHub Actions run 37083503427](https://github.com/mattmedlin/critterlink/actions/runs/37083503427):
all eight Debug/Release jobs on Windows x86-64, Linux x86-64, macOS ARM64 and
macOS x86-64 built with strict warnings and passed all sixteen suites.

## IOP/SIF diagnostic (#12) local validation

On October 2, 2026 (America/New_York), native Apple Silicon Debug, Release and
address/undefined-behavior sanitizer builds passed all nineteen suites. The
affected IOP/SIF/integration suites also passed again in all three configurations
after adding PC/opcode context to IOP failure diagnostics. Earlier ELF, CPU,
interrupt, graphics and vector fixtures remain passing.

Independent tests cover the separate IOP register/RAM model, branch and load
delays, explicit unsupported hazards/accesses, bidirectional DMA, FIFO pressure,
mailboxes, masks, cancellation, bounds and transactional invalid snapshots.
Original guest code on both processors configures the exchange and produces
response 40 from request words 7,9,11,13. The EE also reads that result from SMCOM.
At tick31 the snapshot contains five queued qwords and a pending IOP load;
whole-run and single-step replay reach identical complete state/EE trace at
tick223. `--iop-demo` reports completed exchange and identical replay.

The [IOP](iop.md) and [SIF](sif.md) profiles document strict subsets and timing
policies. These tests do not establish BIOS boot, SIFRPC, IOP interrupt dispatch,
SPU2/SIO2 functionality, or silicon-conformant timing/IRQ behavior.

Remote verification passed for `5bfeefe` in
[GitHub Actions run 37085175296](https://github.com/mattmedlin/critterlink/actions/runs/37085175296):
all eight Debug/Release jobs on Windows x86-64, Linux x86-64, macOS ARM64 and
macOS x86-64 built with strict warnings and passed all nineteen suites.

## SPU2 register-driven diagnostic (#13) local validation

On October 2, 2026 (America/New_York), native Apple Silicon Debug, Release and
address/undefined-behavior sanitizer builds passed all twenty-one suites.
The SPU, IOP halfword, integrated audio and CLI suites were also checked after
review added fixed-pitch snapshot validation and stronger audio expectations.

The original IOP guest uploads ADPCM through TSA/DATA/ATTR, polls transfer
status with LHU, and configures the voice, envelope, routing and key registers
with SH. `--spu-demo` produces 135 logical samples and PCM signature
`13041860280187223349`, verifies nonzero PCM and completed release, and restores
identical full state, PCM and EE trace from a mid-attack sample boundary.
Its literal PCM oracle and whole-stream signature were calculated independently.

Tests exercise all five predictors including both history coefficients and
negative saturation, loop start/end/repeat, all supported ADSR phases, envelope
counters, register-driven stereo mixing, clipping, signed volume, 1x/2x pitch,
manual transfer bounds, mid-transfer replay, invalid MMIO and atomic snapshot
rejection. Earlier CPU, ELF, graphics, vector, interrupt and SIF suites pass.

The [SPU2 profile](spu.md) explicitly limits voices, pitch and envelope modes.
Logical sample timing, no interpolation and integer rounding are diagnostic
policies; these checks do not establish physical SPU2 waveform conformance,
firmware audio support or host audio playback. Umbrella #4 remains open.

Remote verification passed for `a0716c0` in
[GitHub Actions run 37086761240](https://github.com/mattmedlin/critterlink/actions/runs/37086761240):
all eight Debug/Release jobs on Windows x86-64, Linux x86-64, macOS ARM64 and
macOS x86-64 built with strict warnings and passed all twenty-one suites.

## Controller, card and CDVD diagnostic (#14) local validation

On October 2, 2026 (America/New_York), native Apple Silicon Debug, Release and
address/undefined-behavior sanitizer builds passed all twenty-four suites.
The SIO2 and integrated I/O suites also passed after the final pristine-card
snapshot validation and persistence/error test additions.

`--io-demo` executes an original IOP program and reports
`ticks=1185 digital=1 analog=1 card=1 disc=1 replay=identical`. The guest polls a
digital controller, selects analog mode with controller commands, writes and
reads sixteen original bytes on a synthetic card, then reads the second original
CD sector through DMA3. Expected bytes are independently specified; the sectors
have distinct patterns so reading the wrong sector cannot pass.

Scheduled input changes during a pad packet preserve the latched reply and appear
in the later analog poll. Snapshots taken mid-pad, mid-card-write and mid-disc-DMA
reproduce subsequent complete state; pad/disc replay also compares EE traces.
Wrong mounted media is rejected without changing the live system. Unit coverage
includes exported card remount/readback, erase/programming, checksums, protection,
FIFO bounds, absent media, CDVD status/errors, multi-sector transfers and DMA
pause/resume. Earlier CPU, ELF, audio, vector, graphics and interrupt suites pass.

The documented [SIO2](sio2.md) and [CDVD](cdvd.md) profiles use synthetic in-memory
images. Host file persistence, filesystems, card authentication/ECC, commercial
media, complete controller protocols and IOP interrupt dispatch remain unsupported.
Logical transfer timing and undocumented status-bit policies are not hardware
conformance claims. Umbrella #4 stays open pending combined integration in #15.

Remote verification passed for `344f0a8` in
[GitHub Actions run 37089983374](https://github.com/mattmedlin/critterlink/actions/runs/37089983374):
all eight Debug/Release jobs on Windows x86-64, Linux x86-64, macOS ARM64 and
macOS x86-64 built with strict warnings and passed all twenty-four suites.
Windows exposed implicit integer-to-byte conversions in `fill` calls in the
device and its test; explicit byte constants fixed both without changing
behavior or compiler settings. The affected sanitizer suite passed again.

## Combined audiovisual/input restoration (#15) local validation

On October 2, 2026 (America/New_York), native Apple Silicon Debug, Release and
address/undefined-behavior sanitizer builds passed all twenty-five suites.
`--integrated-demo` reports `ticks=194 sprite-pixels=12 samples=194
pcm-signature=15153771150353129381 concurrent=1 input=1 replay=identical`.

At tick95 the same snapshot contains one GIF qword remaining and a pending first
vertex, SPU2 sample7 with retained decoder/envelope state, SIO2 response byte3,
and an unconsumed controller event at tick96. Guest code drives every device.
The expected framebuffer, literal PCM sequence and controller replies were
specified independently and checked in a separate implementation review.

Continuation after restore matches full state, every subsequent stereo sample,
framebuffer, controller FIFO/read cursor, EE instruction trace and per-step IOP
PC/register/branch/load-delay observations. Partitioned budgets agree. A changed
future event affects the later guest-consumed button result while preserving the
current packet latch; an invalid response snapshot leaves live state unchanged.
All prior scheduler, timer/interrupt, DMA, vector, SIF, storage and audio tests pass.

[The integrated fixture](integrated.md) specifies the timing and output oracle.
[Subsystem status](subsystem-status.md) records each implementation boundary.
The result is headless guest-driven diagnostic integration, not BIOS/game
compatibility, complete PS2 fidelity or host audio/display/controller support.

Remote verification on October 3, 2026 passed for `0358203` in
[GitHub Actions run 37136722439](https://github.com/mattmedlin/critterlink/actions/runs/37136722439):
all eight Debug/Release jobs on Windows x86-64, Linux x86-64, macOS ARM64 and
macOS x86-64 built with strict warnings and passed all twenty-five suites.
This completes issue #15. The original diagnostic criteria were insufficient
for the intended full emulator: milestone #4 was reopened on October 3, 2026
with expanded hardware scope. This run does not establish hardware completion.

## Expanded hardware milestone: first EE execution delivery (#16)

On October 3, 2026, the full 28-suite native Apple Silicon Debug, Release and
ASan/UBSan runs passed. New suites cover scalar doubleword arithmetic/shifts,
conditional moves, REGIMM/likely branches and both integer HI/LO pipelines,
including independent edge results and restored execution. Existing integrated
hardware, ELF, I/O and CLI regressions remain green.

[GitHub Actions run 37138552689](https://github.com/mattmedlin/critterlink/actions/runs/37138552689)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`576616b`. The first run exposed signed/unsigned optional comparisons in two test
assertions on MSVC. Explicit unsigned expectations fixed those without weakening
compiler flags; the two affected sanitizer suites passed again.

This is partial delivery under #16 and reopened #4. Trap instructions, unaligned
merges, quadword transfers, other instruction families and broader hardware remain
tracked work. Multiply/divide timing and manual-undefined result cases remain
unverified, with explicit stops for the latter. See [CPU coverage](cpu-coverage.md)
and the [expanded hardware plan](hardware-plan.md); no firmware/game compatibility
is established by these tests.

## EE traps, merge accesses and quadword transfers (#16)

On October 3, 2026, all 31 suites passed native Apple Silicon Debug, Release and
fresh ASan/UBSan builds. New `cpu_trap`, `cpu_merge` and `cpu_quadword` suites
exercise all twelve traps, every merge byte offset and both pair orders, all
quadword address offsets, exception handlers, access failures, byte preservation,
register aliases and restored execution. Independent review checked the EE manual
semantics, especially partial LWR extension and LQ/SQ address masking.

[GitHub Actions run 37139770598](https://github.com/mattmedlin/critterlink/actions/runs/37139770598)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`c88d82f`. The integrated CLI remains ticks=194, sprite-pixels=12, samples=194,
pcm-signature=15153771150353129381, concurrent=1, input=1, replay=identical.

An initial reused sanitizer directory retained a CPU object compiled during
parallel edits, causing the two new memory suites to reject their opcodes. A
probe confirmed the stale LQ decoder. The isolated `build-sanitize-ee-memory`
build passed the complete suite; the original sanitizer directory was then
rebuilt cleanly and all three new suites passed there too. No compiler or test
checks were weakened.

The transfers currently support RAM; merge/quadword MMIO and FIFO accesses remain
explicitly unsupported. Full EE instruction auditing, memory/cache integration
and multiply/divide edge-result/timing validation remain tracked work. This
completes the requested instruction batch, not issue #16 or milestone #4.

## Shared CPU/DMA GIF FIFO (#16 / #20)

On October 3, 2026, all 32 suites passed native Apple Silicon Debug, Release
and ASan/UBSan. The new FIFO suite covers capacity, address aliases, rejected
widths, CPU/DMA ordering, stalled retirement, delay slots, interrupt entry and
ERET during a stalled store, guest sprite rendering, reset and snapshot replay.
The integrated diagnostic retains ticks=194, sprite-pixels=12, samples=194,
pcm-signature=15153771150353129381 and replay=identical.

[GitHub Actions run 37144252848](https://github.com/mattmedlin/critterlink/actions/runs/37144252848)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`0c26479`. Earlier Windows runs exposed a test constant colliding with `stat`
and excessive Debug stack usage from putting all snapshot cases in one function.
Renaming the constant and separating the cases into individual functions fixed
both; the affected suite also passed again locally in Debug and ASan/UBSan.
No checks or compiler flags were weakened.

See [GIF FIFO](gif-fifo.md) for the implemented interface and explicit logical
scheduling policy, and [EE integer audit](ee-integer-audit.md) for remaining
instruction and memory-system gaps. Full graphics-path arbitration, other FIFOs,
physical bus timing and general game compatibility are not established. Issues
#16, #20 and milestone #4 remain open.

## PREF and SYNC functional contract (#16)

On October 3, 2026, all 33 suites passed local Apple Silicon Debug, Release and
ASan/UBSan across the full runs and affected CLI reruns. `cpu_sync` checks every
PREF hint and SYNC stype, nonfaulting data addresses, reserved fields, RAM
ordering, prohibited/annulled delay slots, FIFO store backpressure, barriers with
paused graphics and pending DMA, and restored execution.

The initial runs exposed an obsolete CLI assertion: the demo result word 15 was
expected to fault when executed, but it is valid SYNC. The extended demo now
checks successful retirement there. A separate valid ELF containing SYNC with a
nonzero reserved field preserves the CPU-fault exit/diagnostic assertion. Both
CLI suites then passed in all three configurations; emulator code was unchanged.

[GitHub Actions run 37146443170](https://github.com/mattmedlin/critterlink/actions/runs/37146443170)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`5ff7cca`. The integrated diagnostic expectations remain unchanged.

This is the synchronous interpreter's functional ordering contract. Cache
warming, write-buffer flushing, pending CPU operation timing and bus-cycle
accuracy remain unmodeled; future asynchronous CPU work must extend the barrier
implementation. See [CPU coverage](cpu-coverage.md) and the
[integer audit](ee-integer-audit.md). Issues #16 and #4 remain open.

## SA-register instructions and PLZCW (#17)

On October 3, 2026, all 35 suites passed local Apple Silicon Debug, Release and
ASan/UBSan (110.10, 8.52 and 228.05 seconds respectively). `cpu_sa` covers all
low-bit count combinations, ignored high bits, 64-bit opaque token preservation,
zero-register operands, reserved encodings, delay/annul behavior and a properly
spaced RAM save/restore sequence with System snapshot replay. `cpu_plzcw` checks
every sign-transition position, fixed mixed-word results, source/destination
aliasing, r0, upper-lane preservation, reserved fields and delay-slot replay.

[GitHub Actions run 37159461092](https://github.com/mattmedlin/critterlink/actions/runs/37159461092)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`f13c961`. Existing integrated diagnostic and CLI expectations remain green.

MFSA, MTSA, MTSAB, MTSAH and PLZCW now have functional implementations. SA state
is 64-bit to avoid truncating context tokens. Generated SA values use an emulator
bit-count representation; hardware's opaque encoding and pipeline spacing are
not verified or timed. QFSRV and the remaining packed instructions still stop as
unsupported. See [CPU coverage](cpu-coverage.md) for the exact contract. Issues
#16, #17 and #4 remain open; these tests do not establish full EE/game compatibility.

## Packed logic, immediate shifts and QFSRV (#17)

On October 3, 2026, all 36 local suites passed Apple Silicon Debug, Release and
ASan/UBSan across full runs and the affected decoder-test rerun. The initial
runs found one obsolete negative assertion: `0x7000003f` was expected to stop,
but it is the now-supported PSRAW. The test now supplies a nonzero reserved rs
field; it passed again in all three configurations without changing emulator
code or weakening compiler checks.

`cpu_packed` checks every legal funnel byte/halfword count with a byte-window
oracle, all logical truth-table combinations and immediate shift counts with a
bit-position oracle, both 64-bit lanes, aliases and r0, reserved encodings,
unsupported SA values, undefined halfword right-shift counts and branch slots.
A guest fixture loads two quadwords, saves/restores SA, executes QFSRV/PXOR and
stores independently expected bytes. Restoring while another SA count is active
reproduces the complete System state and subsequent traces.

These eleven instructions extend functional MMI coverage, not cycle accuracy.
SA tokens retain the emulator representation and spacing limitation. Remaining
packed arithmetic, saturation, comparisons, permutation, multiply/divide and
variable lane shifts are still unimplemented. #17 and #4 remain open.

[GitHub Actions run 37160428822](https://github.com/mattmedlin/critterlink/actions/runs/37160428822)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`13bf674`. Existing integrated diagnostic and CLI expectations remain unchanged.

## Packed arithmetic, saturation and comparisons (#17)

On October 3, 2026, all 37 suites passed local Apple Silicon Debug, Release and
ASan/UBSan (114.62, 8.15 and 239.62 seconds respectively). The new
`cpu_packed_arithmetic` suite exercises all 65,536 byte-input pairs for each of
eight operation types, mixed halfword/word boundary vectors, literal word results,
signed saturation endpoints, lane isolation, r0 and aliases, unrelated CPU state,
branch delay/annul behavior and unsupported neighboring encodings.

An original LQ/arithmetic/comparison/SQ guest fixture stores independently
specified saturated sums and comparison masks. Full System state and traces
match after restoring a snapshot taken between the arithmetic and comparisons.
All existing integrated diagnostic and CLI expectations also passed unchanged.

This adds 24 instructions across wrapping add/subtract, signed/unsigned
saturating add/subtract, equality and signed greater-than at B/H/W widths.
Signed saturation follows the manual's stated endpoint semantics; documented
pseudocode/prose inconsistencies are recorded in [CPU coverage](cpu-coverage.md).
These tests do not establish pipeline timing or physical-hardware conformance.
Min/max, absolute value, mixed arithmetic, permutations, packed multiply/divide
and other MMI operations remain incomplete; #17 and #4 remain open.

[GitHub Actions run 37161838307](https://github.com/mattmedlin/critterlink/actions/runs/37161838307)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`84b2fe2` without a platform-specific repair.

## Packed selection, mixed arithmetic and rearrangement (#17)

On October 3, 2026, all 38 suites passed local Apple Silicon Debug, Release and
ASan/UBSan (116.37, 7.96 and 245.81 seconds respectively). The new
`cpu_packed_permute` suite checks explicit byte routes with every source bit,
all 65,536 packed color inputs, color-bit selection/discard, signed min/max and
absolute-value boundaries, literal PADSBH lane results, aliases/r0, reserved
unary fields, delay/annul behavior and replay. A guest interleaves two quadwords
and packs them back into independently expected RAM bytes, restoring full System
state mid-sequence without changing subsequent traces or results.

This adds 29 instructions: min/max and absolute halfwords/words, PADSBH,
PEXT lower/upper and PPAC B/H/W, PCPYH/LD/UD, PINTH/PINTEH, six exchange/reverse/
rotate operations, and PEXT5/PPAC5. Earlier rejection tests were updated where
an unimplemented neighbor became supported; still-unimplemented encodings retain
explicit rejection coverage. Existing integrated and CLI expectations passed.

The PABS minimum-value clamp agrees with the manual and published PS2 hardware
results linked in [CPU coverage](cpu-coverage.md). Other new expectations derive
from the manual and project-authored fixtures; this was not a new physical-hardware
run. Packed HI/LO/multiply/divide, variable shifts, FPU/control and hardware timing
remain unfinished. #17 and #4 remain open.

[GitHub Actions run 37162701817](https://github.com/mattmedlin/critterlink/actions/runs/37162701817)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`2e526fb` without platform-specific repairs.

## Packed HI/LO, word multiply/divide and variable shifts

On October 3, 2026, all 39 suites passed local Apple Silicon Debug, Release and
ASan/UBSan (121.04, 8.27 and 257.28 seconds respectively). The new
`cpu_packed_hilo` suite tests full-width PMFHI/PMFLO/PMTHI/PMTLO;
PMULTW/PMULTUW and PDIVW/PDIVUW; PSLLVW/PSRLVW/PSRAVW. Literal signed and
unsigned boundary vectors distinguish full products in rd from sign-extended
HI/LO words. A bit-routing oracle checks every variable shift count and ignored
source bits. Additional checks cover register aliases/r0, reserved fields,
noncanonical operands, second-lane failure atomicity and delay/annul behavior.
An original LQ/multiply/divide/HI-LO-read/SQ guest produces independently expected
RAM results and identical traces/full-System state after snapshot restoration.
Existing integrated and CLI expectations passed.

These are manual-based functional expectations, not new physical-hardware
measurements. Division by zero and signed overflow remain explicit host stops,
consistent with scalar divide; no guest exception is fabricated. Multiply/divide
latency is unmodeled. Packed accumulates, halfword operations, formatted HI/LO
moves, FPU/control and broader hardware behavior remain unfinished. #17 and #4
remain open. See [CPU coverage](cpu-coverage.md) for exact restrictions.

[GitHub Actions run 37167201054](https://github.com/mattmedlin/critterlink/actions/runs/37167201054)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`c5817f4`. No platform-specific repair was needed.


## Packed word accumulates and formatted HI/LO transfers

On October 4, 2026, all 40 suites passed local Apple Silicon Debug, Release and
ASan/UBSan (124.59, 8.69 and 264.85 seconds respectively). The new
`cpu_packed_accumulate` suite covers PMADDW/PMADDUW/PMSUBW, five PMFHL
formats (LW/UW/SLW/LH/SH) and PMTHL.LW. Literal vectors exercise carry, borrow,
64-bit wraparound, signed/unsigned products and ignored accumulator upper words.
Every source bit is checked against explicit byte routes for LW/UW/LH; signed
word/halfword saturation checks include both endpoints and adjacent values.
PMTHL tests preserved upper words, including an r0 source. Aliases/r0, reserved
fields/formats, atomic noncanonical-source rejection and delay/annul replay are
covered. An original guest initializes and repeatedly accumulates, reads and
stores formatted results, subtracts, and reproduces RAM/state/traces after a
full-System snapshot restore. Existing integrated and CLI checks passed.

The expectations derive from the primary manual linked in [CPU coverage](cpu-coverage.md),
not a new physical-hardware run. Word accumulates require canonical source
halves but accept arbitrary accumulator words. Multiplication timing/interlocks,
packed halfword multiply/accumulate/divide, FPU/control and broader hardware work
remain. #17 and #4 stay open.

[GitHub Actions run 37193903916](https://github.com/mattmedlin/critterlink/actions/runs/37193903916)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`5c3677e`. No platform-specific repairs or weakened checks were needed.


## Packed halfword products and broadcast division

On October 4, 2026, all 41 suites passed local Apple Silicon Debug, Release and
ASan/UBSan (129.41, 9.50 and 274.26 seconds respectively). The new
`cpu_packed_halfword` suite covers PMULTH, PMADDH, PMSUBH, PHMADH, PHMSBH
and PDIVBW. Mixed signed boundary vectors check all eight product lanes,
accumulator wrap and independent HI/LO words. Literal horizontal sum overflow
and signed product results complement the arithmetic oracle. Two published PS2
mixed-input vectors check complete horizontal results, including upper HI/LO
words marked undefined by the manual.

PDIVBW is checked against all 65,535 nonzero divisor encodings using quotient/
remainder identities, magnitude bounds and sign rules. Literal vectors cover
zero divisors, minimum/-1 overflow, negative remainders and ignored divisor bits.
Aliases/r0, reserved rd and neighboring encodings, delay/annul execution, guest
RAM results and full-System replay are covered. The former PHMADH rejection test
now targets a still-reserved encoding. Existing integrated/CLI tests passed.

The [CPU contract](cpu-coverage.md) links the primary manual and pinned published
hardware outputs/input definitions. Horizontal upper-word behavior and zero
divisors follow published results; PDIVBW remainder sign extension follows the
manual's operation/diagram and those results rather than contradictory prose.
No external emulator implementation was imported or new physical-hardware run
performed. MMI inventory/conformance still needs a fresh audit, and timing,
SA hardware encoding, scalar/word divide edge restrictions, FPU/control and
broader system work remain. #17 and #4 stay open.

The first Linux build exposed a test-only C++ portability error: one `auto`
declaration combined a `long long` remainder magnitude with a `long` divisor
magnitude on the LP64 ABI. Splitting the declarations preserves every check.
After that repair, the affected suite passed again in Debug, Release and
ASan/UBSan (4.16, 0.48 and 8.75 seconds). Emulator source did not change.

[GitHub Actions run 37196317838](https://github.com/mattmedlin/critterlink/actions/runs/37196317838)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`23c4572`, including both previously failing Linux configurations.

## MMI inventory audit and signed divide correction

On October 4, 2026, all 42 suites passed local Apple Silicon Debug, Release and
ASan/UBSan (131.33, 9.92 and 280.61 seconds respectively). The new
`cpu_mmi_audit` suite checks all 2,048 MMI function/subopcode pairs against an
explicit primary-manual inventory. With canonical register fields/operands and
a supported SA token, 257 pairs retire and 1,791 reserved/restricted pairs stop
atomically. The [audit](ee-mmi-audit.md) records all 99 named opcode-0x1c entries
(103 forms counting PMFHL formats separately) as having decoder paths; this is
not exhaustive arithmetic conformance or a cycle-accuracy claim.

The review found that DIV, DIV1 and PDIVW incorrectly stopped on signed
minimum/-1 despite the manual's explicit quotient 0x80000000 and remainder
zero. These paths now produce the specified sign-extended word results using
64-bit intermediates. Focused tests cover scalar pipeline isolation, either/both
packed lanes, complete CPU-state preservation, delay/annul execution, guest RAM
outputs and full-System snapshot replay. Prior overflow-rejection expectations
were replaced with positive result assertions; zero-divisor and noncanonical
operand rejection coverage remains. Existing integrated/CLI checks passed.

The audit assigns remaining SA representation, zero-divisor/noncanonical input,
undefined behavior evidence and timing/interlock gaps. COP1/control and broader
system work remain separate major work. No new physical-hardware measurements
were made. #17 and #4 remain open.

[GitHub Actions run 37217545246](https://github.com/mattmedlin/critterlink/actions/runs/37217545246)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`5507b62`. No platform-specific repairs were needed.

## Zero-divisor results and guest SA byte tokens

On October 4, 2026, all 43 suites passed local Apple Silicon Debug, Release and
ASan/UBSan (135.20, 8.96 and 285.16 seconds respectively). DIV/DIVU, their
pipeline-1 forms and PDIVW/PDIVUW now implement published PS2 zero-divisor
results. Signed quotients are -1 for nonnegative dividends and +1 for negative
dividends; unsigned quotients are all-one words. Remainders retain the dividend
word with ordinary sign extension. All packed lanes and scalar pipelines are
independent. Noncanonical operands still reject atomically.

The new `cpu_divide_zero` suite covers zero, positive, negative and minimum-word
vectors in all six forms, either/both packed lanes, full CPU-state preservation,
r0 sources, branch delay/annul behavior, noncanonical rejection after a valid
first lane, and original guest RAM results with full-System replay. Prior
zero-divisor rejection tests were replaced with positive output checks.

MFSA now exposes a four-bit byte count; MTSA masks the source to four bits and
converts it to the internal bit count used by QFSRV and existing snapshots.
`cpu_sa` checks every guest byte token, each source bit, published masking
examples, MTSAB/MTSAH tokens and a literal published MTSA-1/QFSRV byte rotation.
RAM token save/restore, upper-lane preservation and existing funnel/replay tests
pass. Invalid debugger-imported internal counts still stop at QFSRV.

Pinned primary hardware outputs and test definitions are linked in
[CPU coverage](cpu-coverage.md). These are evidence-backed regressions, not new
physical-hardware runs. The manual's save/restore-only software contract and
pipeline spacing restrictions remain documented; observed token values do not
establish cycle accuracy. Noncanonical word operands, broader conformance,
FPU/control and system hardware remain work. #17 and #4 stay open.

[GitHub Actions run 37218711304](https://github.com/mattmedlin/critterlink/actions/runs/37218711304)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`b3b8f0f`. No platform-specific repairs were needed.

## EE FPU register transport and usability exceptions

On October 4, 2026, all 44 suites passed local Apple Silicon Debug, Release and
ASan/UBSan (140.33, 9.21 and 294.17 seconds respectively) for `47ddb1e`.
MFC1/MTC1, CFC1/CTC1 and LWC1/SWC1 now transport raw words with the documented
GPR extension/preservation, FPR0, FCR0 and FCR31 behavior. CPU/System snapshots
include all FPRs, FCR31 and the reserved accumulator; invalid fixed control bits
reject atomically. Status.CU1 enables COP1, while disabled accesses dispatch
ExcCode 11 with Cause.CE=1 before data access or suboperation decoding.

The new `cpu_fpu_register` suite covers every register and control bit, raw
boundary patterns, memory alignment, reserved encodings, disabled accesses with
BEV/nested EXL/delay-slot combinations, enabled delay/annul execution and replay.
An original guest traps, enables CU1 in its handler, returns with ERET, retries
and stores independently expected bits. A handler checkpoint reproduces traces
and full System state. Existing integrated/CLI checks passed.

Windows Debug exposed a signed/unsigned comparison in the test's optional
exception-code assertion. `9d46472` changes its literal to unsigned; the focused
local Debug suite passed again (4.55 seconds). Emulator source is unchanged.

The [FPU contract](fpu.md) cites primary manuals and pinned published hardware
outputs; no new physical-hardware measurements were made. Floating-point
arithmetic, comparisons, conversions, COP1 branches, pipeline timing and broader
hardware remain unimplemented. #17 and #4 remain open.

[GitHub Actions run 37223190208](https://github.com/mattmedlin/critterlink/actions/runs/37223190208)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`9d46472`, including the repaired Windows Debug build.

## COP1 conditional and likely branches

On October 4, 2026, all 45 local Apple Silicon suites passed Debug, Release and
ASan/UBSan (153.75, 11.96 and 304.18 seconds respectively) for `2638b4a`.
BC1F/BC1T and BC1FL/BC1TL now sample FCR31.C, compute signed PC-relative targets,
execute ordinary delay slots and annul untaken likely slots. A delay-slot CTC1
cannot change a destination already sampled by the branch. CU1 gating and the
existing explicit nested-branch restriction remain in effect.

The new `cpu_fpu_branch` suite covers all four forms and both condition values,
zero/positive/negative and extreme signed offsets, preserved state, executed and
annulled slots, slot exceptions with EPC/BD, disabled COP1 exception priority,
reserved selectors, condition sampling and CPU replay. An original guest uses
CTC1 and taken/fallthrough/annulled paths to store the literal result 3 in RAM;
restoring a pending-branch System snapshot reproduces traces and complete state.
Existing integrated and CLI checks passed.

The implementation follows the primary instruction manual, printed pages
345–348; [FPU coverage](fpu.md) records the contract and restrictions. This adds
architectural branch behavior, not pipeline timing or floating-point comparison
and arithmetic execution. #17 and #4 remain open.

[GitHub Actions run 37224291381](https://github.com/mattmedlin/critterlink/actions/runs/37224291381)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`2638b4a`. No platform-specific repairs were needed.

## EE floating-point comparisons

On October 4, 2026, all 46 local Apple Silicon suites passed Debug, Release and
ASan/UBSan (147.82, 10.65 and 305.12 seconds respectively) for `93b8f5f`.
C.F.S/C.EQ.S/C.LT.S/C.LE.S now update only FCR31.C using integer bit ordering:
exponent-zero inputs compare as zero, both zero signs are equal, and exponent
255 remains finite. No host IEEE NaN/denormal behavior is imported.

The new `cpu_fpu_compare` suite checks independently ordered boundary groups,
all register pairs, both initial C values, preservation of every other flag and
register, CU1 exceptions, delay/annul behavior, reserved encodings and replay.
An original compare/branch guest writes literal expected flags and slot-count
results to RAM; a snapshot continuation reproduces traces and full System state.
Existing integrated and CLI checks passed. The primary format/comparison manuals
and pinned hardware outputs are linked in [FPU coverage](fpu.md). These are
reference-based regressions, not new physical-hardware measurements.

Arithmetic, conversions, accumulator operations and pipeline timing remain work,
alongside other architectural and hardware gaps. #17 and #4 remain open.

[GitHub Actions run 37225163553](https://github.com/mattmedlin/critterlink/actions/runs/37225163553)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`93b8f5f`. No platform-specific repairs were needed.

## EE FPU word conversions and sign operations

On October 4, 2026, all 47 local Apple Silicon suites passed Debug, Release and
ASan/UBSan (147.03, 10.64 and 310.03 seconds respectively) for `ca64b82`.
CVT.S.W/CVT.W.S use integer normalization, truncation and signed saturation,
without host floating-point casts. MOV.S copies raw bits; ABS.S/NEG.S change
only the sign and clear current O/U while preserving sticky flags and other
FCR31 state. All five instructions support writable FPR0 and register aliasing.

The new `cpu_fpu_convert` suite covers published literal vectors, every float
exponent, integer powers of two, saturation/truncation boundaries, all register
pairs, raw exponent-zero/255 patterns, full preserved state, reserved fields,
CU1 exceptions and delay/annul behavior. An original guest converts -3, negates,
copies and converts back, writes literal expected results to RAM, then replays
identically from a full-System snapshot. Existing integrated/CLI checks passed.

[FPU coverage](fpu.md) links the primary manuals and pinned published hardware
outputs and now inventories all 34 chapter instructions: 19 have implemented
paths and 15 remain unsupported. This is an instruction inventory, not a
completion percentage. General arithmetic, accumulator overflow state, pipeline
timing and wider hardware remain work. No new physical-hardware measurements
were made. #17 and #4 remain open.

[GitHub Actions run 37226068337](https://github.com/mattmedlin/critterlink/actions/runs/37226068337)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`ca64b82`. No platform-specific repairs were needed.

## EE FPU minimum/maximum operand selection

On October 4, 2026, all 48 local Apple Silicon suites passed Debug, Release and
ASan/UBSan (154.65, 10.47 and 319.25 seconds respectively) for `bbf7c26`.
MIN.S/MAX.S now select and preserve raw operand encodings, including signed-zero
ordering and exponent-zero payloads, and clear current O/U while retaining
other flags. FPR0 and all source/destination aliases are supported.

The new suite checks explicit ascending bit patterns, all register triples,
each writable flag bit, CU1 exceptions, delay/annul and replay. An original guest
stores independently expected +0, -0 and a selected exponent-zero payload and
reproduces traces and complete System state after restoration. A first negative
test accidentally encoded a valid BC1F instruction; that test case was removed
before the full runs. Existing branch tests still cover BC1F.

Primary instruction pages 363/365, Core manual page 163 and pinned published
outputs establish selection, flags, signed-zero handling and representative raw
payload results. The [FPU contract](fpu.md) explicitly distinguishes the broader
raw exponent-zero ordering inference from exhaustive physical-hardware evidence.
No new hardware measurements were made. Arithmetic and pipeline conformance,
alongside broader architectural/hardware work, remain open in #17 and #4.

[GitHub Actions run 37227098556](https://github.com/mattmedlin/critterlink/actions/runs/37227098556)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`bbf7c26`. No platform-specific repairs were needed.

## EE FPU add/subtract and accumulator forms

On October 4, 2026, all 49 local Apple Silicon suites passed Debug, Release and
ASan/UBSan (154.81, 11.15 and 321.15 seconds respectively) for `00946d8`.
ADD.S/SUB.S and ADDA.S/SUBA.S now use integer significand alignment and
normalization, signed-zero and exponent-zero rules, finite exponent 255,
saturation/flush on exponent overflow/underflow, current O/U replacement and
sticky SO/SU accumulation. Non-destination FPRs/ACC and unrelated flags survive.

The suite encodes 72 published ADD/SUB result vectors and checks both FPR and
ACC forms, plus literal exponent-limit, cancellation, signed-zero, flag,
aliasing, CU1, reserved-field and delay/annul cases. An original guest checks
ACC updates and stores literal FPR results to RAM, then reproduces full System
state and traces after restoration. A prior unsupported-opcode test was changed
from the now-implemented ADD.S to a reserved function.

Research during implementation distinguished one retained alignment bit from
an earlier no-extra-bit candidate, despite both passing the basic published
vectors. Explicit distance-24/25 tests now distinguish those models. The
[FPU contract](fpu.md) links the original reverse-engineering notes and clearly
states the evidence limit: those notes describe VU behavior and EE similarity,
not exhaustive independent EE measurements. Precision conformance remains work;
this batch does not claim full hardware fidelity or pipeline timing. No external
emulator implementation or proprietary asset was imported. #17 and #4 stay open.

[GitHub Actions run 37228270881](https://github.com/mattmedlin/critterlink/actions/runs/37228270881)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`00946d8`. No platform-specific repairs were needed.

## EE FPU division

On October 4, 2026, all 50 local Apple Silicon suites passed Debug, Release and
ASan/UBSan (158.02, 12.40 and 327.28 seconds respectively) for `dbe39e0`.
DIV.S uses integer quotient/remainder rounding, preserves finite exponent 255,
handles signed zero and exponent-zero inputs, saturates/flushes exponent limits,
and updates current I/D and sticky SI/SD without changing O/U or other state.

Published hardware vectors include 1/3=0x3eaaaaab and 1/1.5=0x3f2aaaab,
establishing nearest-direction results that ordinary chop arithmetic misses.
The test suite covers pinned basic vectors, all 65,025 nonzero exponent pairs
with exact power-of-two operands and both result signs, flags, aliases, CU1,
delay/annul and original guest RAM/flags output with full-System replay.
No host floating-point cast or ambient rounding mode participates.

[FPU coverage](fpu.md) cites the primary manual and physical-hardware reports,
documents the nearest-even model and its conformance limits, and records an
unresolved RSQRT research result: composing rounded SQRT and DIV misses seven
published result rows. SQRT/RSQRT remain unsupported pending their implementation;
no test-operand special cases were added. No new physical-hardware measurements
were made. Pipeline timing and wider #17/#4 hardware work remain open.

[GitHub Actions run 37229083357](https://github.com/mattmedlin/critterlink/actions/runs/37229083357)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`dbe39e0`. No platform-specific repairs were needed.

## EE FPU square root

On October 4, 2026, all 51 local Apple Silicon suites passed Debug, Release and
ASan/UBSan (204.21, 15.45 and 406.72 seconds respectively) for `7e19413`.
SQRT.S uses integer root extraction and nearest rounding, reads FT, returns a
positive magnitude including positive zero, and updates current I/D and sticky
SI without modifying unrelated flags or ACC. Exponent 255 remains finite.

Tests cover 21 pinned published results, additional physical-hardware reports,
all nonzero exponents using scaled exact/irrational roots, negative exponent-zero
inputs, all FT/FD pairs, preserved state, flags, CU1, reserved FS, delay/annul and
original guest RAM/flags output with full-System replay. Primary evidence and
the manual's negative-zero discrepancy are documented in [FPU coverage](fpu.md).

Initial Windows Release validation exposed a generated expectation of 0x60000000
for input 0x80800000, while the CPU correctly returned 0x20000000. Diagnostic
output was retained and the signed-power sweep was replaced with bounded unsigned
biased-exponent sweeps in `bfe41a5`; no production arithmetic change was needed.
The revised suite passed locally in Debug, Release and ASan/UBSan (3.21, 0.26 and
5.87 seconds). The earlier full-suite results cover unchanged production code.

RSQRT remains unsupported: neither composing ordinary rounded SQRT/DIV nor a
truncated-quotient variant explains every published result. Multiply/accumulate,
precision conformance, pipeline timing and wider hardware work remain open.
The new [memory/reset inventory](memory.md) records the next implementation
requirements without claiming those planned paths already work. #17 and #4
remain open; no firmware or game compatibility is established by these tests.

[GitHub Actions run 37230925924](https://github.com/mattmedlin/critterlink/actions/runs/37230925924)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`bfe41a5`, including the previously failing Windows Release suite.

## Boot ROM backing and EE reset-vector entry

On October 4, 2026, all 53 local Apple Silicon suites passed Debug, Release and
ASan/UBSan (175.85, 12.78 and 366.50 seconds respectively) for `e4c3519`.
Host-supplied ROM bytes now back reads and instruction fetch at physical
0x1fc00000 and its kernel aliases. Loading validates size and copies atomically;
unloaded ranges and writes fail explicitly. Memory/System snapshots include ROM
bytes. A separate CPU entry API selects 0xbfc00000 and the implemented BEV/ERL
reset bits while preserving the existing diagnostic-reset API.

`boot_rom` covers memory widths, aliases, partial and maximum images, edges,
failed/aliased loads, immutable guest accesses and replacement snapshots. Its
original ROM program reads reset Status, stores literal results to RAM, enters
the BEV syscall handler, and reproduces full System state/traces after replacing
the ROM and restoring a checkpoint. `boot_rom_cli` covers bounded execution,
inspection, trace, image and option failures and repeated identical output.
The manually run original ROM produced five retired instructions, PC 0xbfc0000c
and RAM[0x100]=0x2a, including its branch delay slot.

Windows initially rejected a signed integer literal compared with an unsigned
optional exception code under warnings-as-errors. `9748db6` uses an unsigned
expectation; production behavior is unchanged. Both new suites were rerun locally
in Debug, Release and ASan/UBSan (13.77, 0.67 and 27.21 seconds total).

The [memory contract](memory.md) states the current limits: physical-address
conveniences and cache bypass remain diagnostic behavior, reset is partial,
IOP ROM execution is absent, and ROM writes stop rather than inventing hardware
bus-write semantics. No proprietary firmware was used. These results establish
ROM-backed original guest execution, not actual BIOS boot. #18 and #4 stay open.

[GitHub Actions run 37232753794](https://github.com/mattmedlin/critterlink/actions/runs/37232753794)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`9748db6`, including both Windows configurations after the test type correction.

## EE TLB translation, privilege faults and scratchpad

On October 4, 2026, all 54 local Apple Silicon suites passed Debug, Release and
ASan/UBSan (195.05, 14.24 and 373.80 seconds respectively) for `1d4ccfb`.
Boot-vector CPU execution now performs architectural translation and privilege
checks for fetch, scalar/FPU, merge and quadword accesses. The explicit flat
profile remains available for directly initialized diagnostics/ELFs.

The implementation includes 48 TLB entries, seven page sizes, ASID/global and
even/odd matching, MMU register transfers, TLBR/TLBWI/TLBWR/TLBP, replacement
state, refill/invalid/modified exceptions and a separately backed 16-KiB
scratchpad selected by EntryLo0.S. CPU/System snapshots validate and restore the
translation profile, registers, entries, replacement state and scratchpad bytes.

`mmu` tests all page sizes, all slots, frame edges, permissions, register masks,
replacement wrap, invalid states, nested/BEV/delay-slot exceptions and original
mapped-RAM/scratchpad guest output. A real guest refill handler installs a missing
mapping, executes SYNC.P/ERET and retries the load; checkpoint replay reproduces
complete System state and traces. Review caught partial-access fault addresses
being aligned too early; all eight merge forms now have original-effective-address
regressions. Invalid scratchpad snapshot sizes reject atomically.

Initial Windows runs crashed in five snapshot-heavy integration suites after
scratchpad bytes were added inline. Moving scratchpad backing and snapshots to
heap-backed vectors resolved those failures without increasing platform stack
limits or weakening the suites. The final full local runs above include that
storage change and the merge-fault fix.

[GitHub Actions run 37234929333](https://github.com/mattmedlin/critterlink/actions/runs/37234929333)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`1d4ccfb`. The [MMU contract](mmu.md) records deterministic choices for undefined
reset/read values and current limits: cache modes bypass to backing, Random uses
retired instructions rather than cycles, pipeline hazards and scratchpad DMA are
unmodeled, and scratchpad instruction fetch stops explicitly. No proprietary
firmware or new physical-console measurements were used. #18 and #4 stay open;
these results do not establish actual BIOS boot or general game compatibility.


## EE instruction and data caches

On October 4, 2026, all 55 local Apple Silicon suites passed Debug, Release and
ASan/UBSan (184.50, 12.68 and 377.52 seconds) for production commit `d3cb1df`.
The final test-only commit `0e9952a` additionally passed focused `cache|mmu`
checks in all three configurations (9.93, 0.74 and 19.63 seconds).

Architectural execution now implements the Config-controlled 16-KiB instruction
and 8-KiB data caches, writeback/write-through visibility, replacement/locking,
TagLo/TagHi transfers and fourteen CACHE maintenance operations. Original guests
verify dirty data remains separate from RAM until writeback, cached code remains
stale until invalidation, and full System checkpoint replay preserves cache state.
Tests also cover all indices/ways, indexed words, disabled-cache IFL, mapped cache
attributes, quadword and partial writes, privilege/translation faults and invalid
snapshot rejection. See the [cache contract](cache.md) for the exact boundaries.

Linux Release initially rejected an existing MMU test's copied-and-reset
`optional<string>` with GCC's maybe-uninitialized warning. The final test compares
full CPU state against an expected stop record directly, preserving the atomicity
assertion without suppressing diagnostics or changing production behavior.

No proprietary firmware or new physical-console measurements were used. UCAB,
instruction steering/BHT, BTAC and cache/bus timing remain absent. This batch does
not establish BIOS boot or game compatibility; #18 and #4 remain open.

[GitHub Actions run 37240123551](https://github.com/mattmedlin/critterlink/actions/runs/37240123551)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`0e9952a`, including Linux Release after the portable test correction.


## EE uncached accelerated read buffer

On October 4, 2026, all 56 local Apple Silicon suites passed Debug, Release and
ASan/UBSan for production commit `5b5140b` (191.06, 12.89 and 387.42 seconds).
Final test-only commit `d358065` adds full-GIF-FIFO retry coverage; its UCAB suite
passed all three configurations (4.11, 0.56 and 8.48 seconds).

Mode-7 data loads now use a physically tagged 128-byte read buffer. Tests cover
stale data across both 64-byte halves, replacement, mapped aliases, scalar/FPU/
merge/quadword paths, all SYNC stypes, uncached/cached/scratchpad invalidation,
exceptions, IRQ entry, annulled stores, ROM boundaries and snapshot rejection.
An original guest observes old buffered bytes, executes SYNC.L, reads fresh RAM
and stores a literal result. Checkpoint replay reproduces complete System state
and traces. A stalled GIF SQ preserves the buffer; its successful retry clears it.

The [cache contract](cache.md) separates implementation from remaining evidence:
DCE's mode-7 override and the exception invalidation set need physical EE
confirmation. The related C790 manual informs the all-exception interpretation;
it is not a new PS2 console measurement. Write gathering, transfer ordering,
nonblocking overlap and bus timing remain unimplemented. No proprietary firmware
was used, and no BIOS boot or general game compatibility is claimed. #18/#4 stay
open.

[GitHub Actions run 37243258019](https://github.com/mattmedlin/critterlink/actions/runs/37243258019)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`d358065`, including the final FIFO retry regression.


## EE COP0 timer and interrupt controls

On October 4, 2026, all 57 local Apple Silicon suites were validated in Debug,
Release and ASan/UBSan across full runs and affected-test reruns. Production code
is `03e0801`; the final test correction and additional stall check are `b279133`.
Initial full runs took 182.44, 11.60 and 385.04 seconds respectively, with only an
obsolete GIF-stall full-state expectation failing. The assertion now expects two
elapsed Count cycles while still comparing every other CPU field. The corrected
`cop0_timer|gif_fifo` suites passed in 16.20, 0.63 and 34.07 seconds. A preflight SA
full-state expectation was likewise extended by the five supplied run cycles.
No production clock behavior was weakened to preserve the obsolete expectations.

Implemented Count/Compare transfers, wrapping explicit cycle advancement, latched
IP7 and Compare acknowledgement, IM7 delivery, EI/DI with EDI privilege semantics,
and Status.CH updates for DHIN/DHWBIN. Tests include full-width/large advances,
all EI/DI privilege combinations (including mapped user execution with CU0 clear),
interrupt masks, BEV and delay-slot entry, external-source independence, cache
hit/miss reporting and a full FIFO stall interrupted by the timer before acceptance.
An original guest programs Compare, enables interrupts with EI, reads Cause,
acknowledges Compare, increments its service count and returns through ERET.
Pending and in-handler snapshots reproduce complete System state and traces.

`Cpu::step` executes a boundary without assigning elapsed cycles; the explicit
clock API is independent of retirement. Cpu/System runners currently supply one
diagnostic cycle for retirement, exception entry or FIFO stall. Physical issue
latencies, EE/bus ratios, COP0 hazards and write/compare pipeline conformance remain
unmodeled. The [control inventory](interrupts.md) also lists remaining PRId,
bus-error, debug/performance, level-2 and BC0 work. No new console measurements,
firmware boot or general game compatibility are claimed; #17/#18/#4 stay open.

[GitHub Actions run 37245000037](https://github.com/mattmedlin/critterlink/actions/runs/37245000037)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`b279133`, including the corrected stalled-clock expectation and timer preemption.


## IOP integer core, exceptions and shared boot ROM

On October 4, 2026, all 58 local Apple Silicon suites passed Debug, Release and
ASan/UBSan (200.03, 12.72 and 423.85 seconds) for `632c8ba`. The final revision
`43ff91d` only separates/braces statements flagged by GCC misleading-indentation;
it changes neither instruction behavior nor expectations. Focused IOP/core/ROM-CLI
reruns passed Debug, Release and ASan/UBSan (16.72, 0.78 and 36.53 seconds).

The IOP now executes the integer/shift/logic, branch/link, HI/LO and merge families
listed in its [instruction inventory](iop.md), with a COP0 subset, RFE and selected
architectural exceptions. Tests check literal arithmetic/extreme values, shift
positions, unconditional conditional-branch links, delayed merge forwarding in
both pair orders, older load completion on exception, nested status-stack pushes,
delay-slot EPC/BD, unaligned jump-target fetch faults and interrupt gates.
Unsupported zero-divisor results and ordinary overlapping loads remain explicit.

The integrated IOP bus reads the same immutable boot-ROM bytes as the EE, through
a separate fetch interface and width-specific data reads. An original ROM guest
enters its BEV syscall handler, reads EPC with delayed MFC0, returns with JR/RFE
and stores literal RAM outputs. Restoring inside that delayed MFC0 reproduces
full System state/traces even after replacing and restoring the image. Other
checks cover simultaneous EE/IOP ROM execution with separate RAM, ROM widths,
missing bytes, immutable stores and invalid snapshot rejection.

`--bios` now starts both processors. A manual run of the original 20-byte fixture
retired 12 EE instructions, left both PCs at 0xbfc00010 and stored EE RAM[0x100]=42.
The integrated diagnostic retained ticks=194, sprite-pixels=12, samples=194,
pcm-signature=15153771150353129381, concurrent=1, input=1 and replay=identical.
No proprietary firmware was used or actual BIOS boot established. IOP cache/reset
controls, PRId/debug, full INTC/peripheral routing, timers, DMA, physical timing
and independent conformance remain unfinished. #19/#18/#4 stay open.

[GitHub Actions run 37249048634](https://github.com/mattmedlin/critterlink/actions/runs/37249048634)
passed all eight Windows, Linux and macOS ARM64/x64 Debug/Release jobs for
`43ff91dc023c9829382a83db29f022969d04476a`. This includes the final GCC formatting
fix and all 58 suites on each configuration.
