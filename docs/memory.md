# EE memory, reset and firmware implementation inventory

Issue [#18](https://github.com/mattmedlin/critterlink/issues/18) remains open.
The current memory implementation supports diagnostics, not firmware boot.
This inventory establishes the next implementation boundaries; planned entries
below are not claims of implemented behavior.

## Primary-reference requirements

The *EE Core User's Manual* v6.0 describes reset on p. 95, kernel mappings on
pp. 115–117, TLB translation on pp. 118–127, and caches/scratchpad on pp. 129–140
([manual](https://docs.alexrp.com/mips/ee.pdf)). The *EE User's Manual* v6.0,
p. 20, gives the external physical map and boot ROM window
([manual](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).

| Area | Current behavior | Required work |
| --- | --- | --- |
| Main RAM | 32 MiB, little endian, aligned and partial access validation; low virtual addresses are a synthetic identity window | Separate physical decoding from architectural translation and privilege checks; independently establish mirrored/reserved behavior |
| KSEG0/KSEG1 | `0x80000000–0xbfffffff` aliases physical low 512 MiB | Enforce kernel accessibility and cache attributes; KSEG0 cache mode comes from Config.K0, KSEG1 is uncached |
| KUSEG | Diagnostic low-RAM identity access | ERL=1 kernel accesses map directly; ERL=0 accesses require TLB translation |
| KSSEG/KSEG3 | Explicit unsupported translation stop | ASID/TLB lookup, privilege permissions and cache attributes |
| Boot ROM | No backing storage | Load user-supplied bytes into the physical `0x1fc00000` window (maximum 4 MiB); provide reads/fetches through valid mappings and explicit write policy |
| Reset | Synthetic entry address, Status=0 | Enter `0xbfc00000`; set ERL/BEV, clear BEM and Cause.EXC2; initialize documented Config, Random/Wired, cache and debug-control fields as those registers are added |
| Scratchpad | None | 16 KiB, selected by TLB EntryLo0.S, mapped on a 16-KiB boundary; preserve bytes in snapshots and integrate DMA access |
| TLB | None | 48 entries, architectural registers/operations, ASID/global matching, page masks, valid/dirty exceptions, scratchpad restrictions and replacement state |
| Caches | None | I/D cache data/tags, CACHE operations, Config controls, reset invalidation and required visibility behavior |
| MMIO | Explicit documented device subsets | Complete device windows and access-width policies; retain explicit stops for unimplemented hardware |
| Bus errors | Unsupported accesses stop the emulator | Distinguish actual guest bus errors from missing implementation using documented physical decode rules |
| Firmware evidence | No firmware execution | Bounded CLI execution, asset identity and explicit stop diagnostics; reproducible progress through user-supplied firmware without checking its bytes into the repository |

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
