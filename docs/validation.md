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
