# EE memory, reset and firmware implementation inventory

Issue [#18](https://github.com/mattmedlin/critterlink/issues/18) remains open.
The current memory implementation supports diagnostics, not firmware boot.
This inventory distinguishes implemented diagnostic behavior from the remaining
architectural work. Planned entries are not claims of implemented behavior.

## Primary-reference requirements

The *EE Core User's Manual* v6.0 describes reset on p. 95, kernel mappings on
pp. 115–117, TLB translation on pp. 118–127, and caches/scratchpad on pp. 129–140
([manual](https://docs.alexrp.com/mips/ee.pdf)). The *EE User's Manual* v6.0,
p. 20, gives the external physical map and boot ROM window
([manual](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).

| Area | Current behavior | Required work |
| --- | --- | --- |
| Main RAM | 32 MiB, little endian, aligned and partial accesses; architectural CPU translation or explicit diagnostic identity profile | Independently establish mirrored/reserved behavior and full bus-error decode |
| KSEG0/KSEG1 | `0x80000000–0xbfffffff` maps physical low 512 MiB in kernel mode | Functional Config.K0 caching implemented; physical timing and UCAB conformance remain |
| KUSEG | ERL=1 kernel identity access; ERL=0 TLB translation in architectural mode | Cache/timing fidelity |
| KSSEG/KSEG3 | ASID/TLB lookup and privilege permissions | Cache/translation timing and UCAB conformance |
| Boot ROM | Host byte loading, reads/fetches at `0x1fc00000` and kernel aliases, explicit unsupported writes, snapshots | Shared EE/IOP ROM reads/fetches implemented; physical bus-write semantics remain |
| Reset | Diagnostic reset remains available; `reset_boot_vector` enters `0xbfc00000`, sets ERL/BEV and clears BEM/Cause.EXC2 in the implemented subset | Random/Wired initialized to 47/0; Config/cache reset implemented; debug-control reset still required; coordinate IOP/device reset |
| Scratchpad | 16 KiB selected by TLB EntryLo0.S, mapped on a 16-KiB boundary; CPU scalar/partial/quadword data paths and snapshots | Scratchpad DMA, contention, and instruction-fetch behavior |
| TLB | 48 entries, register transfers, TLBR/TLBWI/TLBWR/TLBP, ASID/global matching, seven page sizes, valid/dirty exceptions and replacement state | Physical-cycle Random timing, pipeline hazards and independent hardware conformance |
| Caches | Functional 16-KiB I / 8-KiB D caches, visibility/writeback, locking, Config controls, 14 CACHE operations and a 128-byte UCAB | Instruction steering/BHT, BTAC, UCAB conformance/write gathering, physical timing and remaining conformance; [cache contract](cache.md) |
| MMIO | Explicit documented device subsets | Complete device windows and access-width policies; retain explicit stops for unimplemented hardware |
| Bus errors | Unsupported accesses stop the emulator | Distinguish actual guest bus errors from missing implementation using documented physical decode rules |
| Firmware evidence | Bounded `--bios` CLI and original ROM guest; explicit unsupported-operation/access diagnostics | User-firmware asset identity, reproducible real boot progress, complete reset and hardware paths |

Scratchpad is not inherently fixed at `0x70000000`. Its virtual address is
programmed through the TLB; the manual requires matching V/D bits in the paired
EntryLo registers, zero PageMask, and a contiguous aligned 16-KiB range. A
fixed-address shortcut would hide missing translation behavior.

Reset specifies only selected register bits. Unspecified initial values must be
identified as deterministic emulator choices, not attributed to hardware. The
reset vector is in uncached unmapped kernel space, so instruction fetch there
does not require a preinitialized TLB or cache.

## Implementation sequence and checks

1. Add immutable ROM backing with bounded loading, explicit absence/write faults,
   and snapshot validation. Exercise all supported read widths and aliases,
   edge addresses, failed loads and source-buffer aliasing.
2. Add reset entry and bounded ROM guest execution. Use an original synthetic
   ROM to verify literal RAM results, boot-vector exception handling and replay.
   Expand COP0 reset state without silently accepting unimplemented controls.
3. Separate physical accesses from CPU translation, preserving the diagnostic
   execution mode explicitly. Implement TLB registers/operations, exceptions and
   scratchpad mappings with independent boundary and permission tests.
4. Add cache operations/visibility, scratchpad DMA and remaining physical decode
   behavior alongside firmware-facing IOP and device work.
5. Record real firmware progress and independent homebrew results. Original ROM
   fixtures only establish their covered paths; they do not establish BIOS boot.

Every mutable translation, cache and transfer state must participate in replay.
ROM identity and snapshot replacement failures must be checked before live state
changes. The broader implementation and platform acceptance remain in
[the hardware plan](hardware-plan.md).

## Implemented ROM and reset-entry contract

`Memory::load_boot_rom` copies 1 byte through 4 MiB into the window starting at
physical `0x1fc00000`. Smaller images are useful for original fixtures; missing
bytes are unmapped, not zero-filled or mirrored. Byte/halfword/word/doubleword,
partial and quadword reads are little endian. KSEG0/KSEG1 aliases start at
`0x9fc00000`/`0xbfc00000`. Like the existing diagnostic MMIO API, the canonical
physical address is also accepted by host Memory calls. Boot-vector CPU execution
now performs [architectural translation and privilege checks](mmu.md) before those
physical accesses; direct-initialization fixtures retain their explicit flat profile.

Writes stop explicitly as unsupported; the policy is not a claim about ignored
physical ROM bus writes. Alignment errors retain priority, and the complete
range is validated before access. Failed loads preserve live state; input spans
may alias the existing ROM. Loading does not reset CPU/RAM/devices. `clear()`
continues to clear RAM only. Memory/System snapshots include ROM bytes, allowing
restoration into a different image or an unmounted system. Invalid ROM sizes or
other invalid snapshot fields reject before committing any replacement state.

`Cpu::reset_boot_vector()` initializes the existing CPU state deterministically,
sets PC/next-PC to `0xbfc00000`/`0xbfc00004`, and sets Status to `0x00400004`
(BEV/ERL). Other implemented fields are zero except fixed FCR bits. This is
**not complete hardware reset**: debug registers,
complete IOP and other device reset behavior remain unimplemented. Original IOP
ROM execution and exception handlers are now supported; [IOP limits](iop.md) remain.
Random/Wired initialize to 47/0; Config=0x442 and caches reset invalid. `Cpu::reset(entry)` retains its diagnostic
Status=0 and flat-address behavior; `reset_boot_vector` enables translation.

The CLI constructs a fresh System, loads supplied bytes, calls the boot-vector
entry APIs for EE and IOP and runs a bounded number of existing system boundaries:

```sh
./build/critterlink --bios /path/to/user-rom.bin --steps 1000 --trace
```

It accepts the same `--inspect` and 0–100000 `--steps` options as `--elf`. Exit 0
means the budget expired without an unsupported stop, not successful BIOS boot.
Exit 1 reports loading/access/instruction/device failures; exit 2 reports option
errors. Default budget is 1000. The final IOP PC is also reported. No ROM is supplied, downloaded or recognized by
game-specific behavior. Record a SHA-256 of any user ROM externally when collecting
future firmware results; the CLI does not yet emit an asset hash.

`boot_rom` tests maximum/partial images, widths/aliases, edge ranges, immutable
guest access, failed/aliased loads, replacement snapshots and original reset/BEV
handler execution with literal RAM expectations and full-System replay.
`boot_rom_cli` tests original generated bytes, bounded/zero runs, trace, inspection,
missing/truncated/oversized inputs, bad options and identical repeated output.
No actual PS2 firmware boot has been validated.
