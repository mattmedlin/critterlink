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

The VIF1/VU1 diagnostic, normal channel1 DMA path, and vector fixture were
authored from scratch with Codex/subagent assistance. Original Sony manuals
and primary homebrew tool definitions cited in `docs/vector.md` inform the
documented subset. No vector microprogram from a game or emulator was copied.

The independent IOP interpreter, SIF transport and two-processor diagnostic
were authored from scratch with Codex/subagent assistance. Manufacturer manuals
and primary PS2SDK register/driver references are linked in `docs/iop.md` and
`docs/sif.md`. They were used to establish the limited register and instruction
contracts; no driver implementation, firmware or emulator core was imported.

The SPU2 diagnostic voice engine, IOP halfword instructions and guest sample
upload/playback fixture were authored from scratch with Codex/subagent
assistance. The register and audio arithmetic references are cited in
`docs/spu.md`. Sample blocks and PCM expectations are original synthetic data;
no emulator audio engine, proprietary sample or firmware was imported.

The SIO2/controller/card, CDVD/DMA3 and IOP byte-access implementations were
authored from scratch with Codex/subagent assistance. Primary PS2SDK definitions
and original protocol research are cited in `docs/sio2.md` and `docs/cdvd.md`.
The guest programs, raw card image and sector bytes are original synthetic
fixtures; no card dump, disc image, firmware or emulator implementation was
imported. Host input remains a deterministic recorded sequence.

The combined audiovisual/input fixture is an original coordinated EE/IOP program
using the already documented GIF, SPU2 and SIO2 subsets. Its sprite, sample block,
controller events and expected output sequences are synthetic. No game program,
media or emulator implementation was imported for the integration tests.

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
