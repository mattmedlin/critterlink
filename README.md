# Critterlink

A from-scratch C++ PS2 emulator project targeting Windows, macOS (including
Apple Silicon), and Linux. **This is the milestone 1 foundation, not a working
PS2 emulator. It cannot boot firmware or run games or homebrew.**

## Build and test

Requirements: CMake 3.20+, a C++20 compiler and standard library, and a native
build tool. Recommended toolchains are Visual Studio 2022 with Desktop
development with C++ on Windows, Xcode Command Line Tools (Apple Clang 15+) on
macOS, or GCC 12+ / Clang 15+ on Linux. CMake must be installed separately and
available on PATH. No third-party runtime or test packages are required.

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

On macOS and Linux, run `./build/critterlink --ticks 1000`. With the default
Visual Studio generator, run `.\build\Debug\critterlink.exe --ticks 1000`.
Use a separate `build-release` directory with `Release` in place of `Debug` to
check an optimized build. `CMAKE_BUILD_TYPE` applies to single-configuration
generators; `--config` and `-C` select the configuration for Visual Studio/Xcode.
To build only the core and CLI, configure with `-DBUILD_TESTING=OFF`.

The CLI defaults to zero ticks and exits. `--ticks N` advances the foundation's
logical clock by an unsigned 64-bit decimal count; `--help` prints usage.
Success exits with 0; malformed arguments print to stderr and exit with 2.
Even large counts complete without a real-time wait. These ticks do not yet
represent executed CPU instructions or a calibrated PS2 clock.

## Architecture

- `critterlink_core`: platform-independent static library using only C++ standard
  library facilities. No filesystem, wall clock, randomness, threading, network,
  windowing, or OS APIs are used by the core.
- `critterlink`: small host CLI responsible for arguments and console output.
- `critterlink_tests`: dependency-free checks that remain active in Release.

`MachineState` explicitly holds the current logical tick and two controller
samples. `Machine` owns a validated, immutable input timeline plus its playback
cursor. A reset clears state and rewinds that timeline. The exposed state is a
read-only observation, not a restorable save state; serialization is deferred.

Input events use absolute ticks, zero-based ports, opaque button bits, and four
8-bit axes (neutral 128). Events must be ordered; equal-tick events apply in
provided order. `advance(N)` processes `[current_tick, current_tick + N)`.
Events on the end boundary apply on the next nonzero advance. Zero advancement
does nothing; overflow throws before changing state or consuming input. An
event at the maximum 64-bit tick cannot execute because no later tick exists.
Advancing in different host-sized chunks produces the same observable state at
the same final tick. This is an input-playback foundation, not network sync or
a finished controller implementation.

## Platform coverage

CI configures, builds with warnings treated as errors, and runs core and CLI
checks in both Debug and Release on:

| OS | Architecture | GitHub runner | Toolchain |
| --- | --- | --- | --- |
| Linux | x86-64 | `ubuntu-24.04` | GCC |
| Windows | x86-64 | `windows-2022` | MSVC |
| macOS | ARM64 (native Apple Silicon) | `macos-14` | Apple Clang |
| macOS | x86-64 | `macos-15-intel` | Apple Clang |

The workflow checks runner architecture explicitly. Runner labels follow
[GitHub's hosted-runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
Windows ARM64, Linux ARM64, 32-bit hosts, cross-compilation, and universal macOS
binaries are not currently covered. Local verification is documented in
[docs/validation.md](docs/validation.md); configured CI coverage is not proof of
a successful remote run.

## Scope and provenance

CPU interpretation, RAM/bus, ELF loading, BIOS boot, graphics, sound, storage,
PS2 peripherals, desktop UI, game compatibility, and networking are all
unimplemented. Later milestones will add them incrementally. No compatibility
or performance claims are made by this scaffold.

See [PROVENANCE.md](PROVENANCE.md) for source origin, dependency licensing, and
repository content rules.
