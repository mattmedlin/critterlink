# First homebrew ELF: sum and memory signature

The milestone 3 fixture is a standalone, PS2-targeted bare-metal program using
only the EE scalar instructions already implemented. It is authored here,
redistributable under the [fixture MIT license](../fixtures/sum/LICENSE), and
requires no BIOS, game assets, proprietary SDK, or downloaded binary. It has
been tested in Critterlink, not on physical PS2 hardware. This milestone does
not establish general PS2 homebrew or commercial-game compatibility.

## Build and run

Normal CMake builds generate `build/fixtures/sum.elf` from
[`sum.S`](../fixtures/sum/sum.S). Python 3.8+ is required for this build step;
there are no pip packages. The core library and CLI do not embed Python.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
./build/critterlink --elf build/fixtures/sum.elf --steps 100 --inspect 0x00101010 --inspect 0x00101014 --inspect 0x00101018 --inspect 0x0010101c --inspect 0x00101020
```

For Visual Studio builds, use `.\build\Debug\critterlink.exe` as the executable.
The ELF path remains `build/fixtures/sum.elf`, shared across configurations.
Add `--trace` for the actual instruction stream. `--steps 0` loads without
executing; `--steps 34` stops immediately after the completion marker is stored.
The default is 1000 instructions; allowed budgets are 0–100000. At most 16
`--inspect` options are accepted, each reading one aligned 32-bit RAM word.
Addresses can be decimal or hexadecimal with `0x`. Inspection is read-only.

To reproduce the file directly, without a C++ build:

```sh
python3 fixtures/sum/build.py --output build/fixtures/sum.elf
```

On Windows, `py -3` or the installed `python` can replace `python3`. The source
uses annotated `.word` directives for fixed instruction encodings. The small
builder packs those words into an ELF using Python's standard library and a
fixed link map. It is **not a general assembler/linker or a PS2 SDK**. No external
assembler is required. The generated file is 516 bytes, has no section table,
and has SHA-256:

```text
37c04ec4048dc85f33abfc9c237b34f5d16b1c5e432f6573203508ba36aa64fc
```

CI rebuilds the fixture, checks this same hash on every platform, and executes
the same register/memory assertions. The fixture license covers the source,
builder, and generated ELF; it does not license the rest of the project.

## Known result and runtime

The program sums 5+4+3+2+1, counts five branch delay slots, copies an initialized
data word, increments an initially zero BSS word, then writes a completion
marker. The CPU executes these operations; the host never writes their expected
results. After 34 retired instructions the memory signature is:

| Address | Value | Meaning |
| --- | --- | --- |
| `0x00101010` | `0x4b4e4c43` | Completion marker, bytes `43 4c 4e 4b` (CLNK) |
| `0x00101014` | `0x0000000f` | Sum = 15 |
| `0x00101018` | `0x00000005` | Five delay-slot executions |
| `0x0010101c` | `0x12345678` | Initialized data copied by LW/SW |
| `0x00101020` | `0x00000001` | Zero-filled BSS read, incremented, and stored |

The code segment starts at `0x00100000`, contains 80 bytes, and is marked R/X.
The R/W data segment starts at `0x00101000`, has four file-backed bytes and
36 bytes of memory, so the loader zeroes the remaining 32 bytes. Entry is
`0x00100000`. General registers, HI/LO, SA, COP0 observations, pending branch
state, and prior fault state reset to zero; next PC is entry+4. No stack or GP
is synthesized. This program uses neither a stack nor runtime arguments.

At `0x00100048` the program parks in a jump/NOP loop. After 100 instructions,
PC is again `0x00100048`. `budget-exhausted` is the host's controlled pause, not
a guest exit or HALT instruction. CLI exit 0 means the requested budget ran
without a CPU fault; it does not certify the memory signature or guest success.
The tests explicitly check the signature. CLI argument errors exit 2; load,
file-I/O, inspect-access, and CPU errors exit 1 with diagnostics.

There is no guest console output, syscall interception, kernel service, or
stubbed device. The host reads the ELF from disk, initializes segments/CPU,
limits execution, and prints trace/inspection information. Direct low-RAM
mapping is a bootstrap shortcut, not a functioning PS2 TLB or BIOS boot path.
No graphics, IOP, DMA, interrupts, exception dispatch, FPU, or VU is required
or exercised. The existing [CPU coverage limits](cpu-coverage.md) still apply.

## Accepted ELF profile and failure behavior

The loader accepts a deliberately narrow static format:

- ELF32, little-endian, version 1, System V ABI version 0, ET_EXEC, EM_MIPS.
- MIPS III flags `0x20000000` with optional NOREORDER bit 0. Other ABI/ISA
  extensions, PIC, MIPS16/microMIPS, and additional R5900 metadata are not yet
  accepted. A valid ELF outside this profile receives an explicit error.
- Standard 52-byte ELF header and 32-byte program headers, 1–128 program
  headers, with a file-size limit of 64 MiB.
- PT_LOAD segments wholly within `0x00000000–0x01ffffff`. Destination is
  `p_vaddr`; `p_paddr` is ignored. Kernel-alias segment destinations are rejected.
  File size must not exceed memory size. Overlapping memory ranges are rejected.
- `p_align` is 0, 1, or a power of two with congruent file/virtual offsets.
  Segment permission flags are limited to R/W/X. They validate the entry but
  do not implement memory protection in the current RAM bus.
- The entry is four-byte aligned and its entire first instruction must be in
  the file-backed portion of an executable segment, not BSS or a gap.
- PT_NULL is ignored; NOTE and PHDR file ranges are validated and ignored.
  Other program-header types, including dynamic linking/interpreters, fail.
- Optional section-table and file-backed section ranges are checked, but
  sections do not drive loading. NOBITS sections need no file contents.
  Relocation/dynamic sections and extended section numbering are unsupported.

The loader reads integer fields byte by byte, checks ranges without wrapping,
validates every segment before committing, and stages RAM writes in a copy.
Rejected images leave the entire caller RAM and CPU state unchanged, including
on a failure in a later segment. Successful loads preserve memory outside the
loaded ranges and explicitly zero each segment's BSS. The CLI starts with fresh
zero RAM; API callers can intentionally retain memory outside the image.

Tests cover every truncated prefix of the fixture, invalid headers/flags,
program/section table bounds, late-segment failures, overlapping/out-of-range
segments, entry alignment/permissions/BSS, metadata bounds, file-backed data,
BSS clearing, reset state, bounded execution, and repeatable traces/RAM. They
also verify filenames with spaces and deterministic CLI output on each OS.

The loader was written from the
[System V ABI program-header specification](https://www.sco.com/developers/gabi/2000-07-17/ch5.pheader.html)
and the existing CPU slice, not imported from another emulator.
