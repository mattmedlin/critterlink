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
