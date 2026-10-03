# CPU slice (milestone 2)

This is a from-scratch interpreter for a deliberately small subset of the EE
scalar instruction set. It originally ran synthetic instruction words;
milestone 3 adds the [included bare-metal ELF fixture](homebrew.md). It does not
boot a BIOS or run games. Passing these tests does not establish PS2 compatibility.

## Execution contract

`CpuState` contains 32 GPRs represented as two 64-bit lanes, both HI/LO lanes,
SA, a 32-bit PC, pending next PC, explicit delay-slot context, explicit COP0
registers, and a sticky stop record. Scalar instructions preserve the upper
GPR lane; r0 is always zero, including after debugger state import. HI/LO support
the two integer multiply/divide pipelines. SA uses a 64-bit functional representation
for complete MFSA/MTSA save tokens.

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
| Word arithmetic | ADD, ADDU, SUB, SUBU, ADDI, ADDIU | Low 32-bit arithmetic, sign-extended result; signed forms detect overflow even for r0 destinations |
| Doubleword arithmetic | DADD, DADDU, DSUB, DSUBU, DADDI, DADDIU | Low 64-bit arithmetic; signed overflow dispatches even for r0, upper lane preserved |
| Doubleword shifts | DSLL, DSRL, DSRA, DSLL32, DSRL32, DSRA32, DSLLV, DSRLV, DSRAV | Full 64-bit results; variable counts masked to six bits |
| Conditional moves | MOVZ, MOVN | Test low 64 bits; unchanged destination when condition fails |
| HI/LO | MFHI, MTHI, MFLO, MTLO and pipeline-1 forms | Move full selected 64-bit lane; preserve the other pipeline and upper GPR lane |
| Word multiply/divide | MULT, MULTU, DIV, DIVU, MADD, MADDU and pipeline-1 forms | Sign-extend each 32-bit HI/LO result; multiply/add also writes rd; see restricted cases below |
| Logical | AND, OR, XOR, NOR, ANDI, ORI, XORI, LUI | 64-bit scalar logic, zero-extended logical immediates, sign-extended LUI result |
| Comparison | SLT, SLTU, SLTI, SLTIU | Signed/unsigned 64-bit comparisons; both comparison immediates sign-extended |
| Word shifts | SLL, SRL, SRA, SLLV, SRLV, SRAV | Word result sign extension; variable count masked to five bits; NOP is SLL r0,r0,0 |
| Conditional branches | BEQ, BNE, BLEZ, BGTZ; BLTZ, BGEZ and L/AL/ALL forms; BEQL, BNEL, BLEZL, BGTZL | Signed low-64-bit comparisons; likely forms annul the untaken slot; link forms write PC+8 on both paths |
| Jumps | J, JAL, JR, JALR | Region-preserving immediate target; PC+8 link; delayed transfer; unaligned target faults on subsequent fetch |
| Loads | LB, LBU, LH, LHU, LW, LWU, LD | Signed/unsigned extension, alignment, little-endian assembly, negative offsets |
| Stores | SB, SH, SW, SD | Width truncation, alignment, atomic rejection of invalid accesses |
| Trap comparisons | TGE/TGEU, TLT/TLTU, TEQ/TNE and immediate forms | Low 64-bit comparisons; even unsigned immediate forms sign-extend the immediate; true conditions dispatch code 13 |
| Merge loads/stores | LWL/LWR, LDL/LDR, SWL/SWR, SDL/SDR | Little-endian byte selection in RAM; partial LWR preserves bits 63–32; full access checks for r0 |
| Quadword transfers | LQ, SQ | Transfer both 64-bit lanes in RAM; SQ also writes GIF FIFO with backpressure; mask low four address bits before translation |
| SA transfers/counts | MFSA, MTSA, MTSAB, MTSAH | Full saved-token round trips; byte/halfword shift counts; see representation and timing limits below |
| Leading sign count | PLZCW | Two low-word counts minus the sign bit; preserves the upper GPR lane |
| Hints / ordering | PREF, SYNC, SYNC.L, SYNC.P | Nonfaulting cache hint; barriers in the synchronous interpreter; see contract below |
| Exception instructions | SYSCALL, BREAK | Dispatch general exception; optional instruction code bits accepted |
| COP0 kernel subset | MFC0, MTC0, ERET | Status, Cause, EPC, ErrorEPC and exception/interrupt dispatch; see [interrupts.md](interrupts.md) |

Word operations use low 32-bit operands deterministically; this is not a promise
to reproduce hardware behavior for noncanonical operands that the architecture
declares unpredictable. Control transfer inside a delay slot and JALR with the
same source and link register stop explicitly rather than choosing a hardware
interpretation of unpredictable behavior.

Missing families include the remaining MMI/SIMD operations, CACHE operations,
other COP0 registers/TLB, all FPU/COP1 and VU/COP2 operations. No instruction in these
families is treated as a successful no-op.

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

Manual-undefined noncanonical word operands, division by zero and signed
INT_MIN/-1 division stop explicitly without changing registers or fabricating a
CPU exception. Their actual hardware results require separate validation; these
stops are a remaining compatibility limitation, not a conformance claim. Arithmetic
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

MTSAB XORs the low four source/immediate bits and scales by eight; MTSAH uses
three bits and scales by sixteen. Neither is a REGIMM branch. MFSA/MTSA transfer
the full saved representation through the low GPR lane, preserving the upper
lane. SA is included in CPU/System snapshots and cleared by CPU reset.

The interpreter represents generated SA values as bit counts. Hardware's MFSA
encoding is opaque; these numeric tokens are an emulator convention, not a
verified hardware encoding. Guest code must save and restore them unchanged.
Arbitrary MTSA values are preserved, but no QFSRV result is promised for them.
QFSRV remains unsupported.

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
operands, complete 64-bit snapshot tokens, reserved encodings, delay/annul behavior
and a RAM save/restore sequence. Primary reference: Sony's
[EE instruction manual](https://docs.alexrp.com/mips/ee_insns.pdf), printed pages
148, 151–153 and 215. MFSA/MTSA are save/restore operations; treating their values
as portable software-generated shift counts is not supported by that contract.
