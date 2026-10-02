# Source provenance and dependency licensing

The initial foundation was authored from scratch for Critterlink with Codex
assistance. No existing emulator core, emulator source code, game code, firmware,
or external code snippets were imported. Tests use synthetic controller samples.

The milestone 2 interpreter, memory bus, and literal instruction fixture were
also authored from scratch. Architecture manuals consulted for encodings and
semantics are linked in [docs/cpu-coverage.md](docs/cpu-coverage.md); their text
and example implementations are not vendored. The sum-loop program and expected
results were independently specified for this repository.

Milestone 3 adds an original static ELF loader and a bare-metal homebrew fixture.
The `fixtures/sum` source, builder, and generated ELF are explicitly MIT-licensed
under `fixtures/sum/LICENSE`. No SDK or external program binary is included.
The loader's format reference and fixture's reproducible build/limitations are
documented in [docs/homebrew.md](docs/homebrew.md). Python 3.8+ (PSF License) is
a build-only dependency for packing the fixture; no Python packages or runtime
are linked into Critterlink. See https://docs.python.org/3/license.html.

The milestone 4 foundation (scheduler, timer/INTC/DMA registers, bounded GIF/GS
renderer, digital pad and filter-zero ADPCM primitives) was also authored from
scratch with Codex/subagent assistance. Its small instruction, GIF, serial and
audio fixtures are original synthetic data. Reference manuals and primary SDK
documentation are linked in `docs/hardware-plan.md`, `docs/graphics.md` and
`docs/peripherals.md`. No emulator implementation or media was imported, and no
graphics/audio runtime dependency was added. The foundation does not change the
scope of the existing fixture-only MIT license.

The COP0 dispatch implementation and interrupt handler fixture were authored
from scratch with Codex/subagent assistance. The original Sony core and
instruction manuals referenced in `docs/interrupts.md` informed the supported
register and exception semantics; no emulator source or proprietary guest
binary was imported. The handler consists of original literal instruction words.

There are no vendored or downloaded C++ dependencies. The core, CLI, and tests
use the C++ standard library provided by the selected compiler. Its distribution
and runtime terms depend on the toolchain (MSVC, Apple Clang/libc++, or GCC/libstdc++).
Redistributed binaries will need a toolchain-specific license review.

Build/development tools are not linked into Critterlink:

| Tool | Role | License reference |
| --- | --- | --- |
| CMake / CTest | Configure, build orchestration, tests | BSD 3-Clause; https://cmake.org/licensing/ |
| actions/checkout | GitHub CI source checkout | MIT; https://github.com/actions/checkout/blob/v4/LICENSE |

Apart from the explicitly licensed fixture, no project-wide distribution license has been selected yet. Do not assume an
open-source license merely because the repository is accessible. The owner
should choose one before distributing releases or accepting external code.

Keep proprietary BIOS/firmware, commercial games, disc images, keys, and dumps
out of this repository, test fixtures, CI caches, and artifacts. Future external
dependencies and imported code must record origin, version, license, and any
required notices here before inclusion. Homebrew fixtures must have documented
redistribution permission.
