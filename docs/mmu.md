# EE translation, TLB controls and scratchpad

Boot-vector execution enables `CpuState::architectural_memory`. Every CPU fetch,
scalar/FPU load/store, partial merge, and quadword access then checks alignment,
privilege and translation before touching physical backing. `reset(entry)` and
the existing directly initialized ELF/diagnostic fixtures retain the explicit
flat bootstrap profile. This distinction is saved in CPU/System snapshots.

## Address translation

EXL or ERL forces kernel mode; otherwise Status.KSU selects kernel, supervisor or
user. KSU=3 is rejected. User accesses to addresses at/above 0x80000000 fault;
supervisor mode additionally permits mapped 0xc0000000–0xdfffffff. Kernel
KSEG0/KSEG1 directly map the low 512 MiB of physical space. Kernel KUSEG is
direct while ERL is set, and TLB-mapped otherwise. Remaining permitted ranges
use the TLB. COP0 instructions require kernel mode or Status.CU0; unusable
access dispatches code 11 with CE=0.

The 48 entries contain PageMask, EntryHi and the paired EntryLo values, with
combined G reflected in both low words. Page sizes are 4/16/64/256 KiB and
1/4/16 MiB. Matching masks VPN2 and compares ASID unless both source G bits were
set. Even/odd page selection uses the page-size bit. Low PFN bits below the
page size do not participate in the physical address. Valid/Dirty checks occur
before the physical access. C=2/3/7 are accepted; reserved attributes stop.
**Cache attributes currently share uncached backing**; no cache visibility,
UCAB or physical timing fidelity is claimed.

| Fault | ExcCode | Vector with EXL clear, BEV clear/set |
| --- | --- | --- |
| TLB miss, fetch/load | 2 | 0x80000000 / 0xbfc00200 |
| TLB miss, store | 3 | 0x80000000 / 0xbfc00200 |
| Matching invalid page, fetch/load or store | 2 or 3 | 0x80000180 / 0xbfc00380 |
| Store to valid clean page | 1 | 0x80000180 / 0xbfc00380 |
| Alignment or privilege violation, fetch/load or store | 4 or 5 | 0x80000180 / 0xbfc00380 |

TLB exceptions update BadVAddr, Context.BadVPN2 and EntryHi.VPN2 while preserving
Context.PTEBase and EntryHi.ASID. Nested misses use the common vector. Existing
EPC/BD preservation under EXL and branch-delay restart behavior still apply.
Faulting accesses do not commit register destinations or memory writes.
Physical devices not implemented by the bus remain explicit host stops, not
fabricated guest bus-error behavior. Physical addresses beyond its implemented
low-512-MiB region stop instead of being accidentally reinterpreted as aliases.

## Register and operation contract

MFC0 reads Index(0), Random(1), EntryLo0/1(2/3), Context(4), PageMask(5), Wired(6)
and EntryHi(10). MTC0 writes all except Random. Fixed/reserved bits are masked;
Context writes only PTEBase. PageMask may hold a reserved mask but a TLB write
with it stops. Wired above 47 stops without committing. Status now accepts KSU
and CU0 in addition to the earlier subset.

TLBWI selects Index[5:0]; TLBWR selects Random. TLBR reads an indexed entry and
returns the combined G bit in both EntryLo words. Masked VPN2 bits read zero.
TLBP probes by VPN2/ASID/G, including invalid entries; a miss returns Index.P=1
and low index zero (a deterministic choice for unspecified bits). Out-of-range
indices and multiple matches stop explicitly. TLBR/TLBWI/TLBWR require unmapped
or global instruction space. Unsupported encodings remain stops.

TLB changes take effect at the next interpreter boundary. The real pipeline's
SYNC.P/ERET hazards are not modeled; fixtures include the required barrier.
In architectural mode Random decrements after each retired instruction, wraps
from Wired to 47, and stays at 47 on a Wired write. It does not advance on host
stops, stalls or exception entry. This is a deterministic instruction-based
policy, **not the manual's physical-cycle timing**. Flat diagnostics leave
Random unchanged except explicit Wired writes.

Reset initializes Random/Wired to 47/0. Initial TLB contents are undefined in
the hardware reference; the emulator chooses distinct invalid tags starting
at 0x80000000, separated by 8 KiB. This avoids invented valid mappings and
multiple matches while retaining deterministic TLBR/probe behavior. Snapshot
validation checks register masks, replacement bounds and canonical entries
before live CPU replacement.

## Scratchpad and tests

EntryLo0.S selects 16 KiB of separate scratchpad backing. The mapping must be
16-KiB aligned, PageMask zero, and paired V/D bits equal. C reads as 2; PFN is
ignored for data access. TLBR returns zero PageMask for scratchpad entries, a
deterministic choice where the manual specifies an undefined read value.
All supported CPU data widths, merge paths and LQ/SQ
use the translated offset. There is no fixed 0x70000000 shortcut. Instruction
fetch from scratchpad is explicitly unsupported pending independent evidence.
Scratchpad bytes participate in Memory/System snapshots. `Memory::clear()`
continues to clear main RAM only. Scratchpad DMA/contention remain open.

`mmu` tests all page sizes and edges, all 48 slots, ASID/global behavior,
privilege modes, even/odd frames, clean/invalid/missing pages, register masks,
TLB instructions, Random/Wired, non-default scratchpad mappings, SQ/LQ,
invalid-state rejection, nested/BEV/delay-slot exception entry and fetch faults.
Merge-fault regressions check that BadVAddr reports the original effective
address rather than the aligned start of the partial byte range.
An original guest writes mapped RAM and scratchpad with literal expectations.
A second original handler services a real TLB refill with TLBWI/SYNC.P/ERET,
retries the faulting load and reproduces complete System state/traces from a
checkpoint inside the handler. These are reference-based tests, not physical
console measurements or proof of BIOS boot. #18/#4 remain open.

References: *EE Core User's Manual* v6.0 pp. 63–68, 71, 73–74, 100–102,
113–127 and 139–140 ([manual](https://docs.alexrp.com/mips/ee.pdf));
*EE Core Instruction Set Manual* pp. 337–340
([manual](https://docs.alexrp.com/mips/ee_insns.pdf)). No external emulator
implementation or proprietary firmware was imported.
