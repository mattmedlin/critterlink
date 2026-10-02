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
