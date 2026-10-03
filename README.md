# Critterlink

A from-scratch C++ PS2 emulator project targeting Windows, macOS (including
Apple Silicon), and Linux. **This is an experimental CPU slice, not a working
PS2 emulator. It runs the included bare-metal ELF fixture, but cannot boot
firmware or run games; general homebrew compatibility is not established.**

## Build and test

Requirements: CMake 3.20+, a C++20 compiler and standard library, and a native
build tool. Python 3.8+ is also required to generate the included ELF fixture
(no Python packages are needed). Recommended toolchains are Visual Studio 2022 with Desktop
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
To build only the core and CLI without Python, configure with
`-DBUILD_TESTING=OFF -DCRITTERLINK_BUILD_FIXTURE=OFF`.

The CLI defaults to zero ticks and exits. `--ticks N` advances the foundation's
logical clock by an unsigned 64-bit decimal count; `--help` prints usage.
Success exits with 0; malformed arguments print to stderr and exit with 2.
Even large counts complete without a real-time wait. These ticks do not yet
represent executed CPU instructions or a calibrated PS2 clock.

Run the hand-authored CPU demo with `./build/critterlink --demo`, or add
`--trace` to see instruction addresses, opcodes, and delay slots. It retires 25
instructions and produces r2=15, r3=5, r4=15, r5=143, and RAM[256]=15.
`--demo --steps N` sets a budget from 0 through 100000. Budget exhaustion exits
with 0; an emulator stop prints diagnostics and exits with 1; invalid arguments exit
with 2. A custom budget pauses execution wherever it lands, not necessarily at
the demo's expected endpoint. On Windows use the executable path shown above.
See [CPU coverage](docs/cpu-coverage.md) for the instruction matrix, memory map,
exception limitations, independent demo results, and reference manuals.

The first ELF fixture is built automatically. Run it with:

```sh
./build/critterlink --elf build/fixtures/sum.elf --steps 100 --inspect 0x00101010 --inspect 0x00101014
```

The expected words are `0x4b4e4c43` (completion marker) and `0x0000000f`
(sum = 15). See [homebrew instructions](docs/homebrew.md) for its source/license,
reproducible build, complete signature, accepted ELF profile, and runtime limits.

The first hardware diagnostic is available with `./build/critterlink --hardware-demo`.
It exercises guest-configured timer/INTC/GIF DMA, a headless sprite renderer, and
mid-transfer restoration. It also reports separate digital-pad and filter-zero
audio-decoder checks. Expected output includes `sprite-pixels=12`, `int0=1`,
`int1=1`, and `replay=identical`. Full IOP/SPU2 are
still missing. See the [milestone 4 plan and status](docs/hardware-plan.md).

Guest interrupt handlers can be checked with `./build/critterlink --interrupt-demo`.
Expected: `ticks=128 timer-services=1 dma-services=1 sprite-pixels=12 acknowledged=1
returned=1 replay=identical`. The guest acknowledges timer/DMA interrupts and
returns with ERET. See [the COP0 subset](docs/interrupts.md). Execution budgets
include exception entries, so repeatedly faulting handlers remain bounded.

The VIF1/VU1 diagnostic is available with `./build/critterlink --vector-demo`.
Guest CPU code starts channel1 DMA to upload a microprogram and four data words.
The microprogram calculates a destination address and copies the vector through
a VU register. Expected: `output=7,9,11,13 dma-completed=1 replay=identical`.
This checks the [documented transfer/integer subset](docs/vector.md), including
upload and execution stalls; it does not implement floating-point vector math.

The IOP/SIF diagnostic is available with `./build/critterlink --iop-demo`.
The EE sends a 128-byte block; an independent IOP program adds its first four
words and sends the result back through SIF DMA, also publishing it in a shared
mailbox. Expected: `ticks=223 response=40 exchange-completed=1 replay=identical`.
See the [IOP CPU subset](docs/iop.md) and [SIF transport](docs/sif.md). The IOP is
started explicitly for this fixture; this does not boot firmware or implement
SIFRPC or IOP interrupt dispatch.

The register-driven audio diagnostic is available with `./build/critterlink --spu-demo`.
An IOP program uploads an original ADPCM block through SPU2 transfer registers,
configures a voice and keys it on and off. Expected: `ticks=135 samples=135
signature=13041860280187223349 audible=1 released=1 replay=identical`.
Here `audible=1` means nonzero PCM was generated; there is no host audio output.
The [SPU2 profile](docs/spu.md) supports two core-0 voices, integer pitch,
limited ADSR modes and direct mixing. Interpolation, the physical sample clock,
core 1, reverb and DMA audio transfers remain unsupported.

Run `./build/critterlink --io-demo` for guest-driven controller and media checks.
The IOP polls a digital pad, selects analog mode through controller commands,
writes and reads an original memory-card payload, and reads synthetic CD sectors
through DMA3. Expected: `ticks=1185 digital=1 analog=1 card=1 disc=1 replay=identical`.
Scheduled button/axis changes and in-flight transfers survive restoration.
See [SIO2 and cards](docs/sio2.md) and [CDVD](docs/cdvd.md) for the narrow supported
protocols and media identity checks. Images are synthetic and in memory; no
filesystem, BIOS, commercial disc or general memory-card compatibility is claimed.

## Architecture

- `critterlink_core`: platform-independent static library using only C++ standard
  library facilities. No filesystem, wall clock, randomness, threading, network,
  windowing, or OS APIs are used by the core.
- `critterlink`: small host CLI responsible for arguments and console output.
- `critterlink_tests`: dependency-free checks that remain active in Release.
- `critterlink_cpu_tests`: interpreter, RAM bus, and deterministic execution tests.
- `critterlink_elf_tests`: ELF validation, atomic rejection, loading, and fixture execution.
- `System`: CPU/device scheduling and complete in-memory snapshots; `--elf`
  runs through this coordinated path using one logical device tick per retired
  instruction or guest exception entry (not calibrated PS2 timing).
- Additional scheduler, hardware, system, graphics and peripherals suites test
  the bounded subsystem contracts and restoration.

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

A bounded scalar CPU subset, bootstrap RAM bus, static ELF loader, timer/INTC
register subset, normal GIF DMA and diagnostic sprite renderer are implemented.
BIOS boot, full CPU/COP0 execution, TLB, full VIF/VU and IOP/SIF, full GS/SPU2,
filesystem and host media persistence,
full SIO2/CDVD functionality, desktop UI, game compatibility, and networking remain
unimplemented. Later
milestones will add them incrementally. No game compatibility or performance
claims are made by this slice.

See [PROVENANCE.md](PROVENANCE.md) for source origin, dependency licensing, and
repository content rules.
