# EE integer execution audit — issue #16

Audit date: 2026-10-03, after trap, merge-access and quadword additions.
This compares the decoder in `src/cpu.cpp` with Sony's EE instruction inventory.
It is a source review, not a new conformance run. **#16 remains open.**

## Implemented decoder inventory

The following table records code paths present in this checkout. “Implemented”
means within the restrictions below; it does not imply full memory-system or
pipeline behavior. Detailed test contracts are in [cpu-coverage.md](cpu-coverage.md).

| Family | Implemented mnemonics |
| --- | --- |
| Word arithmetic | ADD, ADDU, ADDI, ADDIU, SUB, SUBU |
| Doubleword arithmetic | DADD, DADDU, DADDI, DADDIU, DSUB, DSUBU |
| Logic / comparisons | AND, ANDI, OR, ORI, XOR, XORI, NOR, LUI, SLT, SLTU, SLTI, SLTIU |
| Word shifts | SLL, SLLV, SRL, SRLV, SRA, SRAV |
| Doubleword shifts | DSLL, DSLL32, DSLLV, DSRL, DSRL32, DSRLV, DSRA, DSRA32, DSRAV |
| Conditional moves | MOVZ, MOVN |
| Branches | BEQ, BEQL, BNE, BNEL, BLEZ, BLEZL, BGTZ, BGTZL, BLTZ, BLTZL, BLTZAL, BLTZALL, BGEZ, BGEZL, BGEZAL, BGEZALL |
| Jumps | J, JAL, JR, JALR |
| HI/LO transfers | MFHI, MFLO, MTHI, MTLO, MFHI1, MFLO1, MTHI1, MTLO1 |
| Word multiply / divide / accumulate | MULT, MULTU, DIV, DIVU, MADD, MADDU, MULT1, MULTU1, DIV1, DIVU1, MADD1, MADDU1 |
| Ordinary loads / stores | LB, LBU, LH, LHU, LW, LWU, LD, SB, SH, SW, SD |
| Merge loads / stores | LWL, LWR, LDL, LDR, SWL, SWR, SDL, SDR |
| Quadword transfers | LQ, SQ |
| Traps | TEQ, TEQI, TNE, TNEI, TGE, TGEI, TGEU, TGEIU, TLT, TLTI, TLTU, TLTIU |
| Exceptions / limited control | SYSCALL, BREAK, MFC0, MTC0, ERET |

NOP is the existing SLL-zero encoding, not a missing independent instruction.
Assembler aliases likewise should not inflate the missing-opcode count.

## Actual gaps and architectural boundary

In the manual's chapter 2 scalar inventory, **PREF and SYNC remain missing**.
SYNC includes the L/P variants. The EE-specific scalar-adjacent omissions are
**MFSA, MTSA, MTSAB, MTSAH and PLZCW**; QFSRV and other packed operations belong
with MMI. These are real gaps, even though the state already contains SA.
See Sony's [instruction manual](https://docs.alexrp.com/mips/ee_insns.pdf),
printed pages 96, 121, 148, 151–153 and the chapter 3 inventory.

Do not add DMULT, DMULTU, DDIV or DDIVU as missing EE arithmetic: their SPECIAL
slots are marked unsupported by EE. Likewise LL/LLD/SC/SCD are not an EE atomic
instruction backlog. Generic MIPS conformance lists are not the EE target.
The same manual's opcode tables, printed pages 395–398, distinguish reserved,
undefined and unsupported encodings.

PREF must not become a faulting ordinary load. SYNC needs explicit ordering and
completion semantics, including its delay-slot restriction; a silent universal
NOP would hide unfinished device behavior. See the instruction manual, pages
96 and 121. The [EE Core manual](https://docs.alexrp.com/mips/ee.pdf), chapters
2–5, supplies the execution, memory and exception context beyond opcode decoding.

## Remaining implementation limitations

- Scalar results preserve upper GPR lanes; LQ replaces both. This is implemented,
  but does not supply packed arithmetic or COP1/COP2 execution.
- Multiply/divide operands must be canonical sign-extended words. Division by
  zero and signed division overflow stop explicitly. Other word arithmetic uses
  low-word operands deterministically; undefined hardware inputs are not proven.
- Branches in delay slots and unpredictable link/source overlaps stop. Ordinary
  taken/untaken slots, likely annulment and trap delay context have focused tests.
- Multiply/divide commit immediately. There is no dual issue, scoreboard, cache,
  write buffer, bus completion model or hardware cycle accounting.
- Merge accesses support RAM only. SQ also supports the shared GIF FIFO with
  backpressure; other quadword MMIO and device byte enables remain unsupported.
  See [GIF FIFO](gif-fifo.md) for the precise implemented transaction contract.
- Address errors, overflow, traps, syscall/break and selected interrupts dispatch
  guest exceptions. Unsupported translation/devices/opcodes still stop on the
  host; they do not establish architectural RI, TLB or coprocessor exceptions.
- COP0 access is restricted to selected registers/status bits. Privilege modes,
  TLB/cache controls, EI/DI, full interrupt control and architectural reset are
  incomplete. Synthetic entry points are not BIOS boot.

## Next work and dependencies

| Work | Tracking / evidence needed |
| --- | --- |
| Complete PREF/SYNC contract | #16 with #17/#27: reserved encodings, nonfaulting prefetch, ordering and delay-slot tests; document any functional timing abstraction |
| SA and PLZCW, then packed execution | #17: exact bit semantics, SA save/restore and QFSRV interaction; keep scalar and packed acceptance separate |
| Extend memory targets | #18: scratchpad, ROM/reset, virtual translation and privilege; test boundaries, aliases and fault precision |
| Device memory transactions | #16 with #20: byte-enable and quadword bus API, full/empty FIFO behavior, no read-modify-write side effects, atomic rejection |
| Full exception semantics | #17/#18: architectural unsupported-instruction and translation dispatch after the required state exists |
| Hardware behavior and completion evidence | #27: latency/interlocks, concurrent DMA ordering, independent hardware observations and replay during pending work |

Before closing #16, reconcile every remaining item with explicit ownership and
acceptance evidence, then run the platform regression matrix and varied guest
fixtures. Opcode coverage alone does not close the memory-operation requirements
or establish BIOS/game compatibility.
