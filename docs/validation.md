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

The Windows, Linux, and Intel macOS jobs are configured but have not yet been
run remotely. A successful GitHub Actions run is still needed before marking
the issue's three-OS fresh-checkout acceptance criterion complete. No PS2
software was run, and no hardware emulation or game compatibility was verified.
