# EE floating-point register, control, branch and comparison support

This implements COP1 register transport, conditional branches, comparisons and access control, not floating-point
arithmetic. All FPR values are raw 32-bit patterns; no host floating-point
conversion, NaN canonicalization or rounding is involved.

## State and instruction contract

CPU/System snapshots include all 32 FPRs, a raw 32-bit accumulator reserved for
later arithmetic, and FCR31. FPR0 is an ordinary writable floating-point register.
Synthetic reset zeros the FPRs/accumulator and sets FCR31 to 0x01000001. This is
a deterministic diagnostic initialization, not a claim about BIOS/reset boot.
Invalid FCR31 snapshot fixed bits reject before replacing live CPU state.

| Instruction | Implemented behavior |
| --- | --- |
| MFC1 | Sign-extend a raw FPR word into the low 64 GPR bits; preserve the upper half; discard GPR r0 writes |
| MTC1 | Copy the low GPR word to any FPR, including FPR0; GPR r0 supplies zero |
| CFC1 | Read FCR0 or FCR31; preserve upper GPR half |
| CTC1 | Write FCR31 writable bits; writes to read-only FCR0 are ignored |
| LWC1 | Read a word through the existing memory bus into any FPR |
| SWC1 | Write the raw FPR word through the existing memory bus |

Transfer encodings require bits 10–0 to be zero. FCR selectors other than 0 and
31 remain explicit unsupported stops, even though published hardware tests show
read aliases. Arithmetic, conversions and accumulator
instructions remain unsupported when CU1 is enabled.

FCR0 is fixed to implementation/revision 0x00002e30, the profile observed in the
published PS2 tests. This is not a claim that every physical revision is identical.
FCR31 writable bits are C (23), I/D/O/U (17–14), and SI/SD/SO/SU (6–3): mask
0x0083c078. Bits 24 and 0 always read one and other bits read zero. CTC1 replaces
writable flag bits directly; arithmetic flag generation is not implemented yet.
All-one CTC1 input therefore reads back 0x0183c079. No FPU interrupt is invented.

LWC1/SWC1 use signed immediate offsets, 32-bit effective addresses, four-byte
alignment and the same RAM aliases/MMIO/fault contract as LW/SW. They do not add
TLB, cache or missing device support. Misalignment dispatches AdEL/AdES (4/5)
without committing FPR or memory changes.

## Conditional branches

BC1F/BC1T and BC1FL/BC1TL sample FCR31.C (bit 23). The target is PC+4 plus
four times the signed 16-bit offset, using 32-bit address arithmetic. Ordinary
forms execute the delay slot whether taken or untaken. Likely forms annul the
slot when untaken, including its memory accesses and exceptions. A slot write
to FCR31 cannot change the previously sampled destination. These instructions
leave registers and FCR flags unchanged.

Only the four manual encodings (rt 0–3) are accepted; other selectors stop
atomically. Enabled branches in delay slots follow the existing explicit
unsupported-nested-branch policy. CU1 gating precedes this policy, so disabled
COP1 still raises code 11 with the existing EPC/BD rules. A fault in an executed
slot reports the COP1 branch's address in EPC with BD set.

`cpu_fpu_branch` covers both condition values, all four forms, positive/negative
and extreme offsets, preserved state, slot execution/annulment, slot faults,
reserved selectors, disabled access, condition sampling and snapshot replay.
An original guest sets C through CTC1, exercises taken/fallthrough/annulled paths,
saves a literal result to RAM and replays from a pending branch checkpoint.
These implement architectural boundary behavior, not FPU pipeline latency.
Primary reference: the instruction manual, printed pages 345–348.

## Comparisons

C.F.S, C.EQ.S, C.LT.S and C.LE.S update only FCR31.C. FPRs, ACC, current
exception flags and sticky flags are preserved. C.F.S always clears C. Both zero
signs compare equal. All exponent-zero inputs compare as zero; exponent 255 is
finite in the EE format, so IEEE NaN/infinity rules do not apply. Nonzero values
are ordered by sign and exponent/fraction magnitude using integer operations,
without host floating-point conversions. The destination/reserved field must be
zero; other comparison functions remain unsupported. CU1, delay/annul and
exception behavior use the same interpreter rules as register operations.

The primary EE Core manual, printed page 156, defines exponent-zero values as
signed zero and exponents 1–255 as finite. Instruction pages 349–352 define the
four comparisons and their C-only result. Published
[comparison outputs](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_fpu/compare.expected)
and [test definitions](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_fpu/compare.cpp)
corroborate signed zeros, exponent-zero flushing and distinct ordered
exponent-255 fractions. The regression suite uses independently ordered value
groups covering these examples and both signs, all register pairs, both initial
C values, full-state preservation, disabled exceptions, delay/annul/replay and
reserved encodings. An original compare/branch guest saves literal flags and
slot-count results to RAM and replays through System snapshots. No new physical
hardware measurements or pipeline timing claims are made.

## COP1 usability and exceptions

COP0 Status.CU1 (bit 29) is now accepted by MTC0 and snapshot restore. The
synthetic reset leaves it clear. A COP1 instruction or LWC1/SWC1 with CU1 clear
enters the general exception vector with ExcCode 11 and Cause.CE=1, without
retirement, FPU mutation or data-bus access. CU1 is checked before data alignment
or supported-suboperation decoding. Existing EPC/BD, nested EXL, BEV vector,
interrupt-priority and ERET rules still apply. Other Status usability/privilege
modes remain unsupported. Cause.CE is updated on coprocessor-unusable entry;
its unspecified value on other exceptions is left unchanged.

COP0 writes take effect on the next interpreter boundary. COP1 operations commit
synchronously. Coprocessor pipeline hazards, interlocks and cycle timing are
not modeled.

## Validation and references

`cpu_fpu_register` tests every FPR index, raw boundary/NaN-like/subnormal-like bit
patterns, upper GPR preservation and r0 semantics, each FCR bit, ignored FCR0
writes, CU1 Status writes, raw word memory traffic, alignment faults, reserved
encodings, snapshot rejection and synthetic reset. Disabled-access tests cover
all six forms, both BEV settings, ordinary/delay-slot entries, nested EXL and
absence of memory side effects. Enabled delay/annul and replay are tested too.
An original guest traps on MTC1, enables CU1 in a handler, returns with ERET,
retries the operation and stores independently expected bits; restoration from
the handler reproduces subsequent traces and complete System state.

Primary references: Sony's [EE Core manual](https://docs.alexrp.com/mips/ee.pdf),
printed pages 73, 75, 157–159 and 164; [instruction manual](https://docs.alexrp.com/mips/ee_insns.pdf),
CFC1/CTC1 pages 353–354, LWC1 page 358, MFC1 page 364, MTC1 page 371
and SWC1 page 379.
Published [FCR outputs](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_fpu/fcr.expected)
and [test definitions](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_fpu/fcr.cpp)
corroborate fixed bits, flag writability, revision value and ignored FCR0 writes.
No external emulator implementation or proprietary program was imported. These
are reference-based tests, not a new physical-hardware run. #17 and #4 remain open.
