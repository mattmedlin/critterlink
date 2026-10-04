# EE integer execution audit — issue #16

Audit date: 2026-10-04, updated through divide-zero and guest SA compatibility corrections.
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
| SA transfers/counts | MFSA, MTSA, MTSAB, MTSAH |
| Packed add/subtract | PADDB/H/W, PSUBB/H/W, PADDSB/H/W, PSUBSB/H/W, PADDUB/H/W, PSUBUB/H/W |
| Packed selection/mixed | PMINH/W, PMAXH/W, PABSH/W, PADSBH |
| Packed rearrangement | PEXTLB/H/W, PEXTUB/H/W, PPACB/H/W, PCPYH/LD/UD, PINTH, PINTEH, PEXEH/CH/EW/CW, PREVH, PROT3W, PEXT5, PPAC5 |
| Packed comparisons | PCEQB/H/W, PCGTB/H/W |
| Packed logical | PAND, POR, PXOR, PNOR |
| Packed HI/LO moves | PMFHI, PMFLO, PMTHI, PMTLO |
| Packed halfword products | PMULTH, PMADDH, PMSUBH, PHMADH, PHMSBH |
| Broadcast divide | PDIVBW |
| Packed word accumulates | PMADDW, PMADDUW, PMSUBW |
| Formatted HI/LO transfers | PMFHL.LW/UW/SLW/LH/SH, PMTHL.LW |
| Packed word multiply/divide | PMULTW, PMULTUW, PDIVW, PDIVUW |
| Packed variable shifts | PSLLVW, PSRLVW, PSRAVW |
| Packed immediate shifts | PSLLH, PSRLH, PSRAH, PSLLW, PSRLW, PSRAW |
| Funnel shift | QFSRV |
| Leading sign count | PLZCW |
| Hints / ordering | PREF, SYNC, SYNC.L, SYNC.P |
| Exceptions / limited control | SYSCALL, BREAK, MFC0, MTC0, ERET |

NOP is the existing SLL-zero encoding, not a missing independent instruction.
Assembler aliases likewise should not inflate the missing-opcode count.

## Actual gaps and architectural boundary

PREF and SYNC now have a tested functional contract in the synchronous
interpreter (see CPU coverage). MFSA, MTSA, MTSAB, MTSAH and PLZCW now have
functional implementations too. QFSRV, packed logical operations and immediate
lane shifts, wrapping/saturating add/subtract and equality/signed greater-than
comparisons, selection/mixed arithmetic, rearrangement, full HI/LO moves,
word multiply/divide/accumulate, formatted HI/LO moves and variable word shifts are implemented;
halfword products and broadcast division are now implemented too. The [MMI audit](ee-mmi-audit.md) finds decoder coverage for all named opcode-0x1c
entries while retaining explicit conformance gaps under #17. SA guest encoding follows published hardware results; pipeline spacing remains
a functional abstraction.
See Sony's [instruction manual](https://docs.alexrp.com/mips/ee_insns.pdf),
printed pages 96, 121, 148, 151–153 and the chapter 3 inventory.

Do not add DMULT, DMULTU, DDIV or DDIVU as missing EE arithmetic: their SPECIAL
slots are marked unsupported by EE. Likewise LL/LLD/SC/SCD are not an EE atomic
instruction backlog. Generic MIPS conformance lists are not the EE target.
The same manual's opcode tables, printed pages 395–398, distinguish reserved,
undefined and unsupported encodings.

PREF is nonfaulting and SYNC enforces its encoding and delay-slot restrictions.
The current interpreter has no pending CPU operations or write buffers when a
barrier is reached. Adding those requires extending the completion contract. See the instruction manual, pages
96 and 121. The [EE Core manual](https://docs.alexrp.com/mips/ee.pdf), chapters
2–5, supplies the execution, memory and exception context beyond opcode decoding.

## Remaining implementation limitations

- Scalar results preserve upper GPR lanes; LQ and full packed results replace both.
  MMI decoder inventory is complete within its documented restrictions; COP1 register/control/memory transport is implemented; COP1 arithmetic and
  COP2 execution remain incomplete.
- Scalar and packed-word multiply/divide operands must be canonical sign-extended words. Zero divisors now
  follow published PS2 results in scalar and packed-word forms; signed minimum/-1
  returns the manual-specified quotient/remainder without an exception.
  PDIVBW accepts all source bit patterns and has explicit zero/overflow results. Other word arithmetic uses
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
| Extend PREF/SYNC with cache/pipeline model | #17/#27: cache behavior, write-buffer flushing, pending operation completion and hardware timing; current functional contract is tested |
| Packed execution and SA hardware behavior | #17/#27: MMI conformance gaps (see [audit](ee-mmi-audit.md)), SA pipeline spacing and wider hardware conformance; implemented instruction tests do not establish full hardware behavior |
| Extend memory targets | #18: scratchpad, ROM/reset, virtual translation and privilege; test boundaries, aliases and fault precision |
| Device memory transactions | #16 with #20: byte-enable and quadword bus API, full/empty FIFO behavior, no read-modify-write side effects, atomic rejection |
| Full exception semantics | #17/#18: architectural unsupported-instruction and translation dispatch after the required state exists |
| Hardware behavior and completion evidence | #27: latency/interlocks, concurrent DMA ordering, independent hardware observations and replay during pending work |

Before closing #16, reconcile every remaining item with explicit ownership and
acceptance evidence, then run the platform regression matrix and varied guest
fixtures. Opcode coverage alone does not close the memory-operation requirements
or establish BIOS/game compatibility.
