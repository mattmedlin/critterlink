# EE functional instruction and data caches

Architectural CPU execution now uses separate 16-KiB instruction and 8-KiB data
caches when Config enables them and the translated cache mode requests caching.
Both are two-way, virtually indexed and physically tagged with 64-byte lines.
Instruction indices use VA[12:6]; data indices use VA[11:6]. The explicit flat
diagnostic profile continues to bypass caches.

## Controls and visibility

MFC0/MTC0 now access Config(16), TagLo(28) and TagHi(29). Config retains fixed
IC/DC size bits 0x440 and writable DIE/ICE/DCE/NBE/BPE/K0 bits 0x73007. Reserved
K0 values reject. Reset chooses K0=2 for its unspecified initial value, giving
Config=0x442, clears all cache tags and retains no dirty data. Data bytes and PFNs
start deterministically at zero when backing is first allocated; hardware reset
does not specify those values.

ICE/DCE disable overrides the address's cache mode. KSEG0 uses Config.K0; mapped
addresses use EntryLo.C. Mode 3 uses writeback and write allocation. K0 mode 0
uses write-through without allocating store misses. Modes 2 and 7 currently use
uncached backing; the mode-7 UCAB remains unimplemented. DIE/NBE/BPE are stored
but dual issue, nonblocking loads, prediction and their timing remain unmodeled.

Dirty data is distinct from RAM until eviction or explicit writeback. Uncached
accesses bypass resident lines; backing-memory writes do not snoop either cache.
Instruction fetch continues to observe cached code until invalidation. CPU scalar,
FPU, partial-merge and LQ/SQ paths use the caches; scratchpad bypasses them.

Replacement follows the XOR of the two LRF bits and flips the selected way's
bit on refill. Ordinary hits do not alter LRF. Instruction refill prefers an
invalid way; data refill excludes locked ways. Writes to locked data lines
change cached bytes without setting Dirty. Re-locking a locked line and locking
both ways stop, matching the manual's undefined-operation restrictions.

Refills and writebacks complete synchronously, without bus contention, missed-
quadword-first timing, pending transfers or nonblocking overlap. A full incoming
line is staged before dirty eviction, so an unsupported backing range cannot
partially replace a line. This failure-staging policy is not a claim about bus
error cycle ordering. RAM/ROM refills and RAM writebacks are implemented;
cached MMIO, cached ROM stores and other physical ranges stop explicitly.

## CACHE instruction coverage

| Operations | Implemented effect |
| --- | --- |
| IXLTG (00), IXSTG (04), IXIN (07) | Read/write indexed instruction tags; invalidate selected way while preserving LRF/PFN |
| IHIN (0b), IFL (0e) | Invalidate matching instruction line; fill using replacement state |
| DXLTG (10), DXSTG (12), DXIN (16) | Read/write indexed data tags including Lock/LRF/Valid/Dirty; invalidate Lock/Valid/Dirty |
| DXLDT (11), DXSDT (13) | Transfer selected cached data word through TagLo |
| DXWBIN (14) | Write dirty indexed line to its stored physical frame and invalidate it |
| DHWBIN (18), DHIN (1a), DHWOIN (1c) | Hit writeback/invalidate, invalidate without writeback, or writeback retaining validity |
| IXLDT (01), IXSDT (05) | Unsupported pending instruction steering/BHT state |
| BXLBT (02), BXSBT (06), BHINBT (0a), BFH (0c) | Unsupported BTAC operations |

Values above are hexadecimal. CACHE requires kernel mode or CU0. Index operations
use the virtual index and bit 0 as way without a TLB lookup. Hit/fill operations
translate even when ICE/DCE is disabled; uncached/accelerated/scratchpad targets
stop because the manual makes those cases undefined. The instruction manual's
DXSTG exception list mentions TLB faults despite its index-only description and
the other index operations' exception lists. The current index-only interpretation
needs independent physical-hardware confirmation. Multiple matching cache ways
also stop; their selection behavior has not been established.

Tag reads zero unspecified bits. Invalid data tags force Dirty clear, following
the Core manual's state invariant despite a contradictory DXSTG pseudocode line.
SYNC barriers remain immediate interpreter boundaries; fixtures include them,
but pipeline hazards are not modeled. PREF remains a nonfaulting ignored hint.
IHIN's BTAC side effect is not implemented because BTAC is absent. Fourteen of
the twenty CACHE operations have paths; this count does not mean cache emulation
or milestone #4 is complete.

## Snapshots and evidence

Cache lines, bytes, tags, Config and TagLo/TagHi are part of CPU/System snapshots.
Heap-backed, lazily allocated lines avoid adding large inline stack objects.
Restore validates sizes, fixed Config bits, reserved modes, tags and lock states
before live replacement. Instruction fills can happen before an instruction
later stops; cache microstate is not equivalent to architectural retirement.

`cache` tests Config masks, enables, all index/way tags, all indexed data words,
LRF eviction, dirty writeback, stale reads/code, lock behavior, byte-enabled writes,
write-through/no-write-allocation, CU0, hit-translation faults and unsupported
paths. An original guest demonstrates old RAM before DHWBIN and new RAM after it;
checkpoint replay restores dirty caches and reproduces full state/traces.
Another guest fetches old code until IHIN. Cached SQ/LQ checks both register lanes.
These are independent reference-based expectations, not new console measurements
or verified firmware boot.

Primary references: *EE Core User's Manual* v6.0 pp. 78, 86–88 and 130–138
([manual](https://docs.alexrp.com/mips/ee.pdf)); *EE Core Instruction Set Manual*
pp. 294–313 ([manual](https://docs.alexrp.com/mips/ee_insns.pdf)). Cache timing,
UCAB, steering/BHT/BTAC, remaining reset controls and broader hardware completion
remain tracked under #18/#4. No external emulator implementation was imported.
