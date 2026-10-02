# Source provenance and dependency licensing

The initial foundation was authored from scratch for Critterlink with Codex
assistance. No existing emulator core, emulator source code, game code, firmware,
or external code snippets were imported. Tests use synthetic controller samples.

The milestone 2 interpreter, memory bus, and literal instruction fixture were
also authored from scratch. Architecture manuals consulted for encodings and
semantics are linked in [docs/cpu-coverage.md](docs/cpu-coverage.md); their text
and example implementations are not vendored. The sum-loop program and expected
results were independently specified for this repository.

There are no vendored or downloaded C++ dependencies. The core, CLI, and tests
use the C++ standard library provided by the selected compiler. Its distribution
and runtime terms depend on the toolchain (MSVC, Apple Clang/libc++, or GCC/libstdc++).
Redistributed binaries will need a toolchain-specific license review.

Build/development tools are not linked into Critterlink:

| Tool | Role | License reference |
| --- | --- | --- |
| CMake / CTest | Configure, build orchestration, tests | BSD 3-Clause; https://cmake.org/licensing/ |
| actions/checkout | GitHub CI source checkout | MIT; https://github.com/actions/checkout/blob/v4/LICENSE |

No project-wide distribution license has been selected yet. Do not assume an
open-source license merely because the repository is accessible. The owner
should choose one before distributing releases or accepting external code.

Keep proprietary BIOS/firmware, commercial games, disc images, keys, and dumps
out of this repository, test fixtures, CI caches, and artifacts. Future external
dependencies and imported code must record origin, version, license, and any
required notices here before inclusion. Homebrew fixtures must have documented
redistribution permission.
