# CPU slice (milestone 2)

This is a from-scratch interpreter for a deliberately small subset of the EE
scalar instruction set. It originally ran synthetic instruction words;
milestone 3 adds the [included bare-metal ELF fixture](homebrew.md). It does not
boot a BIOS or run games. Passing these tests does not establish PS2 compatibility.

## Execution contract

`CpuState` contains 32 GPRs represented as two 64-bit lanes, both HI/LO lanes,
SA, a 32-bit PC, pending next PC, explicit delay-slot context, explicit COP0
registers, FPR/accumulator/FCR31 state, and a sticky stop record. Scalar instructions preserve the upper
GPR lane; r0 is always zero, including after debugger state import. HI/LO support
the two integer multiply/divide pipelines. SA uses an internal bit count; guest MFSA/MTSA expose a four-bit byte count
corroborated by published PS2 hardware tests.

`Cpu::reset(entry)` initializes a synthetic program entry (default zero); it is
not a hardware reset. It does not clear the separately owned `Memory` object.
`restore` imports a debugger snapshot and enforces r0, but assumes the caller
supplies internally consistent PC/delay-slot fields. Resuming a complete session
requires preserving both CPU state and RAM. Save-file serialization is deferred.

`step` samples interrupts, then fetches, decodes, and executes an instruction
when no interrupt is taken. `run(memory, budget)` bounds CPU steps, including
exception entries and bus-stall retries; its retired count includes only completed instructions. Exhausting
a budget is a pause, not a hardware HALT. A zero budget changes nothing. Faulted
instructions do not retire or commit register writes/stores. Earlier retired
instructions remain committed. A stop stays set until reset or debugger restore.

An optional trace vector receives PC, raw opcode (absent on fetch failure),
delay-slot status, retirement status, an optional dispatched exception code, a bus-stall flag, and any stop diagnostic for each CPU
boundary. Traces append across runs; callers control their lifetime/size.
No cycle accuracy, wall time, dual issue, caches, or pipeline stalls are modeled.
The milestone 1 `Machine` input timeline is still separate from CPU stepping;
`--ticks` does not execute instructions. Milestone 4 adds a coordinated `System`
runner used by `--elf`; see [hardware-plan.md](hardware-plan.md). Direct
`Cpu::run` remains CPU-only and does not advance devices.

## Instruction matrix

Only the listed canonical encodings are accepted. Unlisted opcodes, unsupported
COP0 registers, and nonzero reserved encoding fields stop with PC, opcode, and
a reference to this document. An unimplemented valid instruction is an emulator
limitation, not a fabricated architectural Reserved Instruction exception.

| Family | Implemented | Semantics / test focus |
| --- | --- | --- |
| FPU register transport | MFC1, MTC1, CFC1, CTC1, LWC1, SWC1 | Raw bits, FCR masking, CU1 gating and code-11 guest exceptions; [contract](fpu.md) |
| Word arithmetic | ADD, ADDU, SUB, SUBU, ADDI, ADDIU | Low 32-bit arithmetic, sign-extended result; signed forms detect overflow even for r0 destinations |
| Doubleword arithmetic | DADD, DADDU, DSUB, DSUBU, DADDI, DADDIU | Low 64-bit arithmetic; signed overflow dispatches even for r0, upper lane preserved |
| Doubleword shifts | DSLL, DSRL, DSRA, DSLL32, DSRL32, DSRA32, DSLLV, DSRLV, DSRAV | Full 64-bit results; variable counts masked to six bits |
| Conditional moves | MOVZ, MOVN | Test low 64 bits; unchanged destination when condition fails |
| HI/LO | MFHI, MTHI, MFLO, MTLO and pipeline-1 forms | Move full selected 64-bit lane; preserve the other pipeline and upper GPR lane |
| Word multiply/divide | MULT, MULTU, DIV, DIVU, MADD, MADDU and pipeline-1 forms | Sign-extend each 32-bit HI/LO result; multiply/add also writes rd; see restricted cases below |
| Logical | AND, OR, XOR, NOR, ANDI, ORI, XORI, LUI | 64-bit scalar logic, zero-extended logical immediates, sign-extended LUI result |
| Comparison | SLT, SLTU, SLTI, SLTIU | Signed/unsigned 64-bit comparisons; both comparison immediates sign-extended |
| Word shifts | SLL, SRL, SRA, SLLV, SRLV, SRAV | Word result sign extension; variable count masked to five bits; NOP is SLL r0,r0,0 |
| FPU conditional branches | BC1F, BC1T, BC1FL, BC1TL | FCR31.C, signed offsets, delay/annul behavior and CU1 gating; [contract](fpu.md) |
| Conditional branches | BEQ, BNE, BLEZ, BGTZ; BLTZ, BGEZ and L/AL/ALL forms; BEQL, BNEL, BLEZL, BGTZL | Signed low-64-bit comparisons; likely forms annul the untaken slot; link forms write PC+8 on both paths |
| Jumps | J, JAL, JR, JALR | Region-preserving immediate target; PC+8 link; delayed transfer; unaligned target faults on subsequent fetch |
| Loads | LB, LBU, LH, LHU, LW, LWU, LD | Signed/unsigned extension, alignment, little-endian assembly, negative offsets |
| Stores | SB, SH, SW, SD | Width truncation, alignment, atomic rejection of invalid accesses |
| Trap comparisons | TGE/TGEU, TLT/TLTU, TEQ/TNE and immediate forms | Low 64-bit comparisons; even unsigned immediate forms sign-extend the immediate; true conditions dispatch code 13 |
| Merge loads/stores | LWL/LWR, LDL/LDR, SWL/SWR, SDL/SDR | Little-endian byte selection in RAM; partial LWR preserves bits 63–32; full access checks for r0 |
| Quadword transfers | LQ, SQ | Transfer both 64-bit lanes in RAM; SQ also writes GIF FIFO with backpressure; mask low four address bits before translation |
| SA transfers/counts | MFSA, MTSA, MTSAB, MTSAH | Full saved-token round trips; byte/halfword shift counts; see representation and timing limits below |
| Packed add/subtract | PADD/PSUB B/H/W, PADDS/PSUBS B/H/W, PADDU/PSUBU B/H/W | Independent wrapping, signed saturation, or unsigned saturation in every lane |
| Packed selection/mixed arithmetic | PMINH/W, PMAXH/W, PABSH/W, PADSBH | Signed selection, saturated absolute value, low-half subtract/high-half add |
| Packed rearrangement | PEXTL/U B/H/W, PPAC B/H/W, PCPYH/LD/UD, PINTH, PINTEH, PEXEH/CH/EW/CW, PREVH, PROT3W, PEXT5, PPAC5 | Explicit lane routing and 1-5-5-5 color conversions |
| Packed comparisons | PCEQ B/H/W, PCGT B/H/W | Equality and signed greater-than; all-one/zero lane masks |
| Packed logical | PAND, POR, PXOR, PNOR | Full 128-bit Boolean results; both destination lanes replaced |
| Packed HI/LO moves | PMFHI, PMFLO, PMTHI, PMTLO | Full 128-bit transfers with reserved-field checks |
| Packed halfword products | PMULTH, PMADDH, PMSUBH, PHMADH, PHMSBH | Eight signed products; lane-local accumulation or horizontal combination; hardware-derived upper words |
| Broadcast word divide | PDIVBW | Four signed words divided by the low signed halfword; signed remainders and verified edge cases |
| Packed word accumulates | PMADDW, PMADDUW, PMSUBW | Two modulo-64-bit accumulators assembled from low HI/LO words; full rd results |
| Formatted HI/LO transfers | PMFHL.LW/UW/SLW/LH/SH, PMTHL.LW | Word/halfword routing, signed saturation, preserved upper words on PMTHL |
| Packed word multiply/divide | PMULTW, PMULTUW, PDIVW, PDIVUW | Two independent word operations; full products in rd; sign-extended HI/LO words; restricted divide inputs below |
| Packed variable shifts | PSLLVW, PSRLVW, PSRAVW | Low word of each doubleword; independent five-bit counts; sign-extended doubleword results |
| Packed immediate shifts | PSLLH, PSRLH, PSRAH, PSLLW, PSRLW, PSRAW | Independent 16/32-bit lanes; see halfword count restrictions below |
| Funnel shift | QFSRV | SA-controlled 256-bit concatenation, low 128-bit result |
| Leading sign count | PLZCW | Two low-word counts minus the sign bit; preserves the upper GPR lane |
| Hints / ordering | PREF, SYNC, SYNC.L, SYNC.P | Nonfaulting cache hint; barriers in the synchronous interpreter; see contract below |
| Exception instructions | SYSCALL, BREAK | Dispatch general exception; optional instruction code bits accepted |
| COP0 kernel subset | MFC0, MTC0, ERET | Status, Cause, EPC, ErrorEPC and exception/interrupt dispatch; see [interrupts.md](interrupts.md) |

Word operations use low 32-bit operands deterministically; this is not a promise
to reproduce hardware behavior for noncanonical operands that the architecture
declares unpredictable. Control transfer inside a delay slot and JALR with the
same source and link register stop explicitly rather than choosing a hardware
interpretation of unpredictable behavior.

Remaining architectural work includes MMI conformance/timing, CACHE operations,
other COP0 registers/TLB, FPU arithmetic/comparisons/conversions and VU/COP2
operations. Unimplemented instructions stop explicitly.

## Memory and exception staging

The bus owns 32 MiB of zero-initialized RAM. Addresses `0x00000000–0x01ffffff`
are a **synthetic identity-mapped bootstrap window**, not an implemented user
TLB. Kernel aliases `0x80000000–0x81ffffff` and `0xa0000000–0xa1ffffff` share
those bytes via direct translation. Cache attributes and privilege permissions
are not modeled. Other kernel direct-map addresses fail as unimplemented memory
or devices; other virtual addresses fail as unsupported translation. There is
no arbitrary address masking into RAM, BIOS, scratchpad, or RAM mirroring.
Milestone 4 adds explicit timer/INTC/GIF-DMA MMIO within the EE hardware window;
other registers still fail. See [the register profile](hardware-plan.md).

The scalar bus accepts widths 1, 2, 4, and 8. Separate RAM APIs support selected
byte ranges and aligned 16-byte quadwords. Each store validates its entire range
before writing, and all transfers explicitly assemble little-endian values
without host pointer casts. Effective addresses wrap at 32 bits. Loads to r0 still perform
access checks. Host calls with invalid widths throw `std::invalid_argument`;
guest access failures become actionable CPU stops.

For arithmetic overflow (code 12), address errors (load/fetch 4, store 5),
SYSCALL (8), BREAK (9), and true trap comparisons (13), the CPU enters a guest
exception handler.
INTC/DMAC lines can dispatch interrupts when enabled. EPC/BD preserve delay-slot
context, EXL suppresses interrupt reentry, and ERET resumes the saved PC.
See [interrupts.md](interrupts.md) for masks, nested exceptions, vectors,
step budgets, and explicit limitations. Unsupported translation/devices still
produce host-side stops without fabricated COP0 exceptions.

## Independent demo oracle

The fixture in `include/critterlink/demo.hpp` contains nine literal words,
authored for this project. The expected answers follow from the algorithm,
not from this interpreter's output: add integers 5, 4, 3, 2, 1; count the five
loop delay slots; store/reload the sum; OR it with 128.

| Address | Word | Instruction / purpose |
| --- | --- | --- |
| 0x00 | 24010005 | ADDIU r1,r0,5 |
| 0x04 | 24020000 | ADDIU r2,r0,0 |
| 0x08 | 00411021 | ADDU r2,r2,r1 |
| 0x0c | 2421ffff | ADDIU r1,r1,-1 |
| 0x10 | 1420fffd | BNE r1,r0,0x08 |
| 0x14 | 24630001 | ADDIU r3,r3,1 (delay slot) |
| 0x18 | ac020100 | SW r2,0x100(r0) |
| 0x1c | 8c040100 | LW r4,0x100(r0) |
| 0x20 | 34850080 | ORI r5,r4,0x80 |

After exactly 25 retired instructions: PC=36, next PC=40, r1=0, r2=15,
r3=5, r4=15, r5=143. Other GPRs and upper lanes remain zero. RAM at 0x100
contains the bytes `0f 00 00 00`. There is no pending delay slot or fault.
There is no sentinel HALT opcode: the caller stops at this known budget.
Continuing beyond the program executes the following RAM contents and can fault.

Tests compare complete CPU states, traces, and RAM from one bounded run versus
single-step runs and reset/replay. Separate tables check each implemented
instruction and include literal overflow/extension boundary results, taken and
untaken branches, aliased memory, and failed instructions. These tests establish
the stated slice's behavior, not conformance of an entire EE processor.

## References

Semantics and encodings were consulted in primary architecture manuals; no
emulator implementation was imported:

- Sony Computer Entertainment, *EE Core User's Manual*, version 6.0, chapters
  2–5 ([publicly accessible mirror](https://docs.alexrp.com/mips/ee.pdf)).
- MIPS Technologies, *MIPS64 Architecture for Programmers, Volume II*, revision
  0.95 ([university-hosted copy](https://www.ece.lsu.edu/ee4720/mips64v2.pdf)).

These documents are references, not redistributed project content or grants of
license. The generic MIPS manual does not establish support for EE-specific
instructions; the implemented scope above is authoritative for Critterlink.

## Expanded milestone 4 scalar work

Issue #16 begins with doubleword arithmetic/shifts and conditional moves.
Boundary tests cover signed overflow without host signed-arithmetic overflow,
upper-lane preservation, six-bit variable shifts, reserved encodings, destination
aliasing and restored instruction traces. Sony's *EE Core Instruction Set Manual*
version 6.0, pages 47–64 and 87–88, is the primary semantic reference
([manual mirror](https://docs.alexrp.com/mips/ee_insns.pdf)). This extension does
not complete #16 or the EE instruction set.

REGIMM and likely branches follow the same manual, pages 31–45. Link forms write
PC+8 regardless of branch outcome; rs=31 for these forms is rejected as
unpredictable. An untaken likely branch skips the slot entirely, including fetch,
MMIO and exception effects. Tests cover both paths, signed 64-bit boundaries,
negative offsets, link preservation, delay exceptions and mid-branch restoration.
BLTZALL follows the explicit annul pseudocode and likely-branch definition; its
prose contains a contradictory delay-slot sentence.

Both integer multiply/divide pipelines now use the existing HI/LO state lanes.
Multiply-add concatenates the low words of the selected HI/LO pair and adds the
product modulo 64 bits. Results in each HI/LO lane and the multiply rd destination
are sign-extended words, including unsigned operations. Moves transfer the entire
selected 64-bit lane. These operations commit immediately; pipeline latency and
interlock timing remain part of #27.

Manual-undefined noncanonical word operands stop explicitly without changing
registers or fabricating a CPU exception. Zero-divisor results now follow published
PS2 hardware evidence, as documented below. Signed INT_MIN/-1
now produces sign-extended quotient 0x80000000 and remainder zero, as specified
by the manual; the [MMI audit](ee-mmi-audit.md) records the correction. Noncanonical operands remain a compatibility limitation. Arithmetic
is implemented without host signed overflow. Tests cover both pipelines, signed
and unsigned products/quotients/remainders, accumulator carry/wrap, lane isolation,
r0 destinations, invalid encodings and restored mixed-pipeline execution.

HI/LO and multiply/divide references are the same manual, pages 51–53, 85–92,
138–150 and 152–157. MADD1 follows the operation's high HI/LO lanes; the prose's
reference to the low pipeline conflicts with that operation and instruction name.

## Traps, merge accesses and quadwords (#16)

All twelve trap encodings compare low 64-bit operands. Register-form code fields
are accepted without changing the comparison. REGIMM immediate traps can execute
in branch delay slots; they are not classified as branch instructions. A true
condition enters the existing general exception path with code 13, preserving
EPC/BD rules, EXL behavior and BEV vector selection. A false condition retires
without changing registers. Tests execute a guest handler that advances EPC and
returns through ERET, including restored continuation from inside the handler.

Merge accesses use the original effective address and select bytes within its
containing aligned word or doubleword. LWL always sign-extends its merged word;
LWR sign-extends only when offset zero loads the entire word, otherwise keeping
old bits 63–32. LDL/LDR merge within the low 64 bits. These loads preserve the
upper 64-bit GPR lane. Left/right pairs work in either order without an inserted
NOP; they remain two separate instructions, so a successful first store is not
rolled back if the second instruction faults.

LQ/SQ mask the effective address's low four bits before translation, as specified
by the EE instruction set. Misalignment is therefore not an address exception.
LQ loads both register lanes; SQ writes both lanes. Loading r0 still accesses
memory, while storing r0 writes sixteen zero bytes. Effective address arithmetic
wraps at 32 bits before masking. The aligned host bus API rejects misalignment;
masking is instruction behavior, not a general weakening of memory validation.

Selected-byte stores validate before mutation and write only the affected bytes.
They never read old MMIO values to synthesize a masked write. Merge MMIO accesses
still stop explicitly, even when selecting a full word. SQ now supports the
[GIF FIFO](gif-fifo.md); other quadword MMIO accesses remain unsupported. These
are backend limits, not ISA prohibitions. Other FIFOs, byte-enabled device
accesses, TLB faults and cache behavior remain tracked in #17/#18/#20.

`cpu_merge` checks literal results at every byte offset, both pair orders,
signed/negative/wrapped addresses, aliases, neighboring-byte preservation and
replay between pair instructions. `cpu_quadword` checks all sixteen misaligned
offsets, exact byte order, source/base aliasing, RAM endpoints, error atomicity and
full-System restoration. `cpu_trap` tests all comparisons, delay/annul behavior,
exception entry and the guest handler.

Primary reference: the same Sony instruction manual, printed pages 72–75,
80–83, 99–102, 117–134, 141 and 287. LWR's pseudocode condition `byte = 4`
conflicts with its two-bit byte index; its prose and byte diagram specify full
word sign extension at offset zero. The implementation follows those consistent
prose/table semantics and tests partial loads with noncanonical old upper bits.

## FIFO backpressure and audit

A full GIF FIFO leaves SQ unretired, preserves PC/register/delay-slot state and
sets `InstructionTrace::stalled` without creating a host stop or guest exception.
`System::run` advances devices on that boundary and retries on the next boundary;
interrupts are sampled before each retry. `Cpu::run` alone does not advance devices,
so it can exhaust its budget on repeated stalls without changing the queue.
See [the FIFO contract](gif-fifo.md) for ordering and remaining timing limitations.

The [complete integer inventory audit](ee-integer-audit.md) distinguishes remaining
EE instructions from generic MIPS operations that the EE does not implement.

## PREF and SYNC (#16)

PREF accepts all 32 hints and performs no data access in the current cacheless
model. Invalid, unaligned, untranslated and device addresses do not cause a data
fault or device read. Instruction fetch and interrupt entry still follow the
normal CPU boundary rules.

SYNC accepts all 32 stypes: bit 4 selects memory ordering (L) or pipeline ordering
(P). Nonzero rs/rt/rd fields stop. Executing either form in a branch delay slot
stops without committing state; an annulled slot is not executed.

The interpreter completes each supported CPU operation before retirement. RAM
stores are committed immediately; FIFO stores retire after queue acceptance, or
stall before a subsequent SYNC can execute. Both barriers are therefore already
satisfied when reached. An accepted FIFO entry need not have been decoded, and
SYNC does not wait for independent DMA or graphics work. This is an explicit
functional abstraction, not a cache flush or cycle-accurate bus implementation.
Future pending loads, write buffers and asynchronous CPU operations must extend
this contract before being enabled.

`cpu_sync` covers every hint/stype, reserved encoding bits, RAM ordering,
delay/annul behavior, full-FIFO stalls, pending DMA and snapshot replay. Primary
reference: Sony's [EE instruction manual](https://docs.alexrp.com/mips/ee_insns.pdf),
printed pages 96 and 121.

## SA registers and PLZCW (#17)

MTSAB XORs the low four source/immediate bits; MTSAH XORs three bits and
doubles the result. These produce the guest-visible byte count. Neither is a
REGIMM branch. MFSA returns that count (0–15) through the low GPR lane and
preserves its upper lane. MTSA consumes only the low four source bits.

Internally, SA remains a bit count (guest byte count multiplied by eight) for
QFSRV and existing in-memory snapshots. It is included in CPU/System snapshots
and cleared by CPU reset. QFSRV rejects noncanonical debugger-imported internal
values (anything other than a multiple of eight from 0 through 120). Guest
MTSA/MTSAB/MTSAH always produce canonical internal values. Internal snapshot
values are not guest save tokens; no persistent save-file format exists yet.

Published PS2 tests show MTSA 16 reading back as zero, 0xffff as 15, and MTSA 1
rotating a repeated QFSRV source by one byte. These observations correct the
former emulator-only bit-count token exposed by MFSA. The manual still requires
saving/restoring tokens unchanged; accepting arbitrary low-nibble MTSA inputs
follows the observed hardware behavior rather than expanding that software contract.

SA writes complete immediately here. Hardware's three-instruction spacing rules
before MTSA and after SA reads before MTSAB/MTSAH are not enforced or timed.
Properly spaced programs are the compatibility target; results for violating
those pipeline restrictions are not a hardware-conformance claim. The context
fixture follows the spacing rules. Pipeline scheduling remains tracked in #27.

PLZCW counts matching sign bits minus one separately in each low 32-bit word.
All-zero/all-one words yield 31; opposite top two bits yield zero. It preserves
bits 127..64 and handles source/destination aliasing and r0 normally. Tests cover
every transition position, fixed mixed-word answers, reserved fields and replay.

`cpu_sa` also checks all low input combinations, ignored high bits, zero-register
operands, all 16 byte tokens, high source-bit masking, reserved encodings, delay/annul behavior
and a RAM save/restore sequence. Primary reference: Sony's
[EE instruction manual](https://docs.alexrp.com/mips/ee_insns.pdf), printed pages
148, 151–153 and 215. MFSA/MTSA are save/restore operations; treating their values
as portable software-generated shift counts is not supported by that contract.

## Packed logical and shift operations (#17)

PAND/POR/PXOR/PNOR update all 128 destination bits. QFSRV concatenates rs above
rt, shifts by the SA count and keeps the low 128 bits. Zero selects rt; equal
sources rotate a quadword. The implementation handles zero and 64-bit boundaries
without undefined host shifts, captures sources before writing aliased destinations,
and preserves r0 and unrelated state. Saved/restored canonical SA tokens work;
pipeline spacing remains the limitation described above.

PSLLH/PSRLH/PSRAH process eight independent halfwords; the W forms process four
words. Arithmetic right shifts fill each lane with its own sign. Word counts use
all five instruction bits. PSLLH uses the low four bits; PSRLH/PSRAH explicitly
stop for counts 16–31, which the manual marks undefined despite their low-bit
pseudocode. Nonzero reserved rs fields stop before writes. Neighboring MMI
suboperations remain unsupported; these additions do not enable entire groups.

`cpu_packed` uses a byte-window oracle for all QFSRV counts and bit-position
oracles for packed logic and shifts, with aliases, zero registers, rejected values,
branch slots and original guest RAM output. The guest saves/restores SA using
proper instruction spacing and replays from a snapshot while a different count
is active. Primary reference: Sony's [EE instruction manual](https://docs.alexrp.com/mips/ee_insns.pdf),
printed pages 176, 252–253, 261, 263–264, 266–267, 269 and 285–286.
Later arithmetic/comparison, rearrangement and packed multiply/divide additions
follow below. Full MMI conformance and timing remain unaudited.

## Packed add/subtract, saturation and comparisons (#17)

PADD/PSUB operate independently on sixteen bytes, eight halfwords or four words;
results wrap within each lane. PADDS/PSUBS clamp signed results to each lane's
minimum/maximum. PADDU clamps unsigned addition to the maximum and PSUBU clamps
unsigned subtraction to zero. No lane carries into its neighbor and none of these
operations raises a scalar overflow exception.

PCEQ produces an all-one lane for equality, otherwise zero. PCGT does the same
for signed greater-than, including the sign boundary. All operations replace
both 64-bit destination halves, capture sources before aliased writes, preserve
r0 and leave HI/LO, SA and COP0 unchanged. This does not model pipeline latency.

`cpu_packed_arithmetic` checks every byte-input pair for each operation, mixed
halfword/word boundary vectors, literal word answers, source/destination aliases,
zero-register inputs, branch delay/annul behavior and unchanged unrelated state.
An original LQ/arithmetic/comparison/SQ guest fixture writes independently
specified results to RAM and replays identically from a mid-program snapshot.
Neighboring unimplemented MMI encodings still stop explicitly.

Primary reference: Sony's [EE instruction manual](https://docs.alexrp.com/mips/ee_insns.pdf),
printed pages 160–175, 177–188 and 270–284. Signed saturation follows the stated
signed range and saturation definition: PADDSW's printed underflow interval has
an impossible upper bound, and PSUBSH's prose says truncation despite its
saturation title and endpoint-selecting operation. Tests explicitly cover both
saturation endpoints. These are manual-based functional expectations, not new
physical-hardware measurements. Later selection and rearrangement coverage is
described below.

## Packed selection and rearrangement (#17)

PMIN/PMAX select signed halfword/word values. PABS saturates the most-negative
lane to the largest positive value. PADSBH subtracts in the low four halfwords
and adds in the high four, with lane-local wrap. All results replace both register
halves and preserve unrelated CPU state.

The rearrangement family implements lower/upper interleaving, even-lane packing,
copying, halfword interleaving, center/even exchanges, halfword reversal within
each doubleword and three-word rotation. PCPYUD's source order differs from
PCPYLD. PEXT5/PPAC5 convert 1-5-5-5 and 8-8-8-8 fields separately in each word;
packing clears that word's upper half rather than densely packing the register.
Unary forms reject nonzero reserved rs bits before any mutation.

`cpu_packed_permute` checks every source bit against explicit byte-route tables,
all 65,536 packed color inputs, discarded color bits, signed boundary vectors,
literal PADSBH output, aliases/r0, reserved fields, delay/annul behavior and replay.
A guest loads two quadwords, interleaves their halves and packs them back to an
independently expected RAM result, with full-System restoration mid-sequence.

Primary reference: Sony's [EE instruction manual](https://docs.alexrp.com/mips/ee_insns.pdf),
printed pages 158–159, 175, 189–191, 198–208, 213–214, 222–225, 235–238 and
254–260. PEXTLH's final destination range has a printed width typo; its earlier
lanes and diagram define a full interleaved 32-bit pair. PEXEW follows its explicit
operation/diagram. Published [PS2 hardware results](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_simd/arithmetic.expected)
corroborate the PABSH/PABSW minimum-value clamp. No external emulator code was
imported; these are project-authored tests, not a new physical-hardware run.
Additional architectural conformance and timing work remains.


## Packed HI/LO, word multiplication/division and variable shifts (#17)

PMFHI/PMFLO and PMTHI/PMTLO transfer all 128 bits. Reserved source/destination
fields reject before mutation. PMULTW/PMULTUW independently multiply the low
32-bit word of each 64-bit source half. Each full product goes into the matching
rd half, while its low/high words are separately sign-extended into LO/HI.
PDIVW/PDIVUW place sign-extended quotient/remainder words into LO/HI. Unsigned
operations also sign-extend these stored words. An r0 multiply destination still
updates both HI/LO halves.

The manual requires canonical sign-extended 32-bit operands in each source
half, including unsigned multiplication/division. Noncanonical operands stop.
Zero divisors produce hardware-derived results as documented below. Signed minimum/-1
division returns sign-extended quotient 0x80000000 and remainder zero, without
a guest exception. Both lanes
commit atomically, so an unsupported second lane leaves the first unchanged.
Multiplication/division latency and pipeline hazards remain unmodeled.

PSLLVW/PSRLVW/PSRAVW operate on two words, not all four: bits 31–0 and 95–64
of rt, with independent counts from bits 4–0 and 68–64 of rs. Every result is
sign-extended to its 64-bit half, including logical shifts with count zero.
Other source bits are ignored; these shifts do not require canonical operands.

`cpu_packed_hilo` covers literal signed/unsigned boundary products and divisions,
full-width moves, aliases/r0, every shift count with a bit-routing oracle,
reserved fields, second-lane failure atomicity, delay/annul execution and replay.
An original guest loads quadwords, multiplies/divides, reads HI/LO and stores
independently expected results, including full-System snapshot restoration.

Primary reference: Sony's [EE instruction manual](https://docs.alexrp.com/mips/ee_insns.pdf),
printed pages 194–197, 226–227, 234, 243, 245, 248–251, 262–263, 265–266 and
268–269. Expectations are manual-based functional tests, not a new hardware run.
Later halfword multiply/accumulate/divide additions follow below. FPU/control
and broader system work remain. #17 and #4 stay open.


## Packed word accumulates and formatted HI/LO transfers (#17)

PMADDW/PMADDUW/PMSUBW assemble two 64-bit accumulators from the low word
of each HI half followed by the low word of the corresponding LO half. Upper
words of HI/LO are ignored on input. Signed or unsigned word products are added,
or signed products subtracted, modulo 64 bits without a guest overflow exception.
The full results replace rd; individual HI/LO words are sign-extended into their
64-bit halves. Canonical sign-extended source operands are required even for
PMADDUW, with atomic rejection if either half is noncanonical. r0 discards only
the GPR result. Unsigned host accumulation avoids signed overflow.

PMFHL.LW/UW select lower/upper words from HI/LO. PMFHL.LH truncates each
accumulator word to a halfword; PMFHL.SH instead saturates each signed word to
[-32768,32767]. PMFHL.SLW assembles each accumulator as above, saturates its
signed 64-bit value to signed 32-bit range, and sign-extends the result. PMTHL.LW
writes the four input words to the low words of the HI/LO halves while preserving
every upper word. These transfers accept arbitrary bit patterns; all reserved
fields and unsupported format values reject before mutation.

`cpu_packed_accumulate` checks literal carry/borrow/wrap and signed/unsigned
product vectors, ignored accumulator upper words, every source bit for LW/UW/LH
routing, signed saturation boundaries, preserved PMTHL words, aliases/r0,
noncanonical operand atomicity, reserved formats/fields, branch slots and replay.
An original guest initializes, repeatedly accumulates, reads and stores formatted
results, subtracts, and reproduces RAM/state/traces after full-System restoration.

Primary reference: Sony's [EE instruction manual](https://docs.alexrp.com/mips/ee_insns.pdf),
printed pages 218–221, 227–233, 241–242 and 244. These are functional manual-based
expectations, not a new physical-hardware run. Asynchronous multiply timing,
interlocks, FPU/control and broader system
work remain. #17 and #4 stay open.


## Packed halfword multiply/accumulate and broadcast divide (#17)

PMULTH, PMADDH and PMSUBH multiply eight signed halfword pairs and place their
32-bit products in LO words 0/1, HI words 0/1, LO words 2/3, HI words 2/3.
Accumulates add/subtract the corresponding existing words modulo 32 bits; rd
receives the even product lanes. PHMADH adds adjacent products; PHMSBH subtracts
the even product from the odd product. Their four results go to rd and the low
word of each HI/LO half. Arbitrary source bit patterns are accepted and source
capture preserves aliases; r0 destinations still update HI/LO.

The manual marks the horizontal upper HI/LO words undefined. Published PS2
results show the odd product for PHMADH and its bitwise complement for PHMSBH;
the implementation uses this evidence-derived behavior. Fixed mixed-sign vectors
check the complete outputs, alongside independent signed-arithmetic tests.

PDIVBW divides all four signed source words by the signed low halfword of rt.
Other divisor bits are ignored. Remainders are sign-extended to 32-bit words:
the manual's prose says zero extension, but its operation, diagram and published
hardware results agree on sign extension. Minimum signed word divided by -1
returns 0x80000000 and remainder zero as specified in the programming notes.
For zero divisors, published results support quotient -1 for nonnegative inputs
or +1 for negative inputs and the original dividend as remainder; this behavior
is implemented here. Scalar/packed-word zero-divisor handling is now implemented too. No guest exception is raised for these PDIVBW edge cases.

`cpu_packed_halfword` checks mixed signed boundary products, wraparound, all
65,535 nonzero divisor encodings through quotient/remainder identities, literal
zero/overflow/negative-remainder vectors, ignored divisor bits, aliases/r0,
reserved rd/neighbor encodings, delay/annul execution and full-System replay.
An original guest multiplies, accumulates, subtracts, combines horizontally and
divides, storing independently specified RAM outputs. A former negative PHMADH
case now checks a still-reserved neighboring suboperation.

Primary references: Sony's [EE instruction manual](https://docs.alexrp.com/mips/ee_insns.pdf),
printed pages 192–193, 209–212, 216–217, 239–240 and 246–247;
[published PS2 test outputs](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_simd/muldiv.expected)
and their [input definitions](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_simd/shared.h).
No external emulator implementation was imported; this is not a new physical
hardware run. The [MMI inventory audit](ee-mmi-audit.md) confirms all named opcode-0x1c
entries have decoder paths, with conformance gaps still explicit. Asynchronous timing/interlocks, SA pipeline spacing, FPU/control and
broader system work remain. #17 and #4 stay open.


## MMI inventory and signed divide correction (#17)

The [MMI audit](ee-mmi-audit.md) publishes all subgroup and direct-function
entries, distinguishes decoder coverage from hardware conformance, and assigns
remaining gaps. `cpu_mmi_audit` sweeps all 2,048 function/subopcode pairs under
canonical operands, accepting the 257 legal/restricted-policy pairs and checking
atomic rejection of the others. Detailed arithmetic suites remain necessary.

DIV, DIV1 and PDIVW now implement the manual-specified signed minimum/-1 result:
quotient 0x80000000, remainder zero, with word sign extension and no guest
exception. The correction uses existing signed 64-bit intermediates without
host overflow. Scalar pipeline isolation, either/both packed lanes, preserved
state, delay/annul behavior, guest RAM output and full-System replay are tested.
Noncanonical word operands remain explicit stops in these paths; zero-divisor
results are now implemented as described below. #17 and #4 remain open.


## Zero-divisor and guest SA compatibility (#17)

DIV/DIV1/PDIVW with a zero divisor return quotient -1 for nonnegative dividends
and +1 for negative dividends. DIVU/DIVU1/PDIVUW return all-one quotient words.
All retain the dividend word as remainder, with normal sign extension into the
corresponding 64-bit HI/LO halves. Packed lanes act independently. No guest
exception occurs. Noncanonical word operands still stop atomically, including a
bad second packed lane when the first lane has a zero divisor.

`cpu_divide_zero` checks fixed zero/positive/negative/minimum-word vectors in
all six forms, each/both packed lanes, untouched scalar pipelines, complete CPU
state, r0 sources, delay/annul behavior, noncanonical rejection and full-System
replay with independent guest RAM results. `cpu_sa` now verifies all 16 guest
byte tokens, MTSA masking of every source bit, generated byte/halfword tokens,
the published one-byte QFSRV rotation and RAM save/restore. Existing funnel
count/routing tests retain the internal bit-count representation.

Primary hardware evidence is pinned to ps2autotests revision
97469ffbed8631277b94e28d01dabd702aa97ef3:
[scalar divide outputs](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee/muldiv.expected),
[packed divide outputs](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_simd/muldiv.expected),
[SA outputs](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_simd/funnel.expected)
and [SA test definitions](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_simd/funnel.cpp).
No external emulator implementation was imported; these are regressions against
published results, not new physical-hardware measurements. Pipeline timing,
noncanonical word operands, broader conformance and FPU/control remain work.


## COP1 register foundation (#17)

MFC1/MTC1, CFC1/CTC1 and LWC1/SWC1 now have raw-bit functional paths with
snapshot state and Status.CU1 gating. Disabled accesses raise code 11 with
Cause.CE=1 and normal EPC/BD handling. FCR0/FCR31 behavior and explicit
unsupported modes are documented in [FPU coverage](fpu.md). No floating-point
arithmetic, comparisons or conversions are implemented yet. BC1F/BC1T and
BC1FL/BC1TL now implement condition sampling, delay/annul behavior and replay;
see the FPU contract and `cpu_fpu_branch` tests.
