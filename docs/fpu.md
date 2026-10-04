# EE floating-point support

This implements COP1 register transport, conditional branches, comparisons,
word conversions, sign operations, raw min/max selection, add/subtract and division. FPRs store raw 32-bit patterns; all
implemented operations use deterministic integer logic, not host floating-point
conversion, NaN canonicalization or rounding.

## Instruction inventory

The primary instruction manual's COP1 chapter (printed pages 342–379) lists
34 instructions. Twenty-six have implemented architectural paths below; eight
remain explicit unsupported operations. This inventory counts instructions,
not hardware fidelity, pipeline completion or milestone progress.

| Instruction(s) | Count | Status | Manual pages |
| --- | ---: | --- | --- |
| MFC1, MTC1, CFC1, CTC1, LWC1, SWC1 | 6 | Implemented within register/bus restrictions | 353–354, 358, 364, 371, 379 |
| BC1F, BC1FL, BC1T, BC1TL | 4 | Implemented architectural delay/annul behavior | 345–348 |
| C.EQ.S, C.F.S, C.LE.S, C.LT.S | 4 | Implemented EE comparisons | 349–352 |
| CVT.S.W, CVT.W.S | 2 | Implemented integer-based conversions | 355–356 |
| ABS.S, MOV.S, NEG.S | 3 | Implemented raw bit/sign behavior | 342, 366, 374 |
| ADD.S, ADDA.S, SUB.S, SUBA.S | 4 | Implemented integer significand arithmetic and flags | 343–344, 377–378 |
| MUL.S, MULA.S | 2 | Unimplemented | 372–373 |
| MADD.S, MADDA.S, MSUB.S, MSUBA.S | 4 | Unimplemented | 359–362, 367–370 |
| MAX.S, MIN.S | 2 | Implemented raw operand selection | 363, 365 |
| DIV.S | 1 | Implemented integer quotient/remainder rounding and I/D flags | 357 |
| SQRT.S, RSQRT.S | 2 | Unimplemented | 375–376 |

Additional work includes arithmetic flag generation, accumulator overflow state,
interlocks and hardware timing; FCR alias behavior remains restricted as below.

## State and instruction contract

CPU/System snapshots include all 32 FPRs, a raw 32-bit accumulator used by
ADDA/SUBA, and FCR31. FPR0 is an ordinary writable floating-point register.
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
read aliases. Multiply, multiply-accumulate and square-root instructions remain
unsupported when CU1 is enabled.

FCR0 is fixed to implementation/revision 0x00002e30, the profile observed in the
published PS2 tests. This is not a claim that every physical revision is identical.
FCR31 writable bits are C (23), I/D/O/U (17–14), and SI/SD/SO/SU (6–3): mask
0x0083c078. Bits 24 and 0 always read one and other bits read zero. CTC1 replaces
writable flag bits directly; ADD/SUB and their accumulator forms generate O/U
and sticky SO/SU.
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

## Word conversions and sign operations

CVT.S.W reads a signed 32-bit word from an FPR, finds its leading bit and packs
an EE floating-point result, truncating discarded fraction bits toward zero.
This includes the signed minimum without host signed overflow. CVT.W.S truncates
the EE value toward zero, returns zero below magnitude one, and clamps biased
exponents above 0x9d to 0x7fffffff or 0x80000000 according to sign. Exponent-255
values therefore saturate rather than becoming host NaNs. Neither conversion
changes FCR31 flags or ACC.

MOV.S copies every bit and preserves flags. ABS.S clears the sign bit; NEG.S
flips it. Both preserve exponent-zero fraction bits and exponent-255 patterns,
and clear current O/U (bits 15/14) while retaining sticky flags, I/D and C.
The unused ft field must be zero. FPR0 is writable and source/destination aliasing
is supported. CU1, delay/annul and snapshot rules are unchanged.

Primary instruction references are pages 342, 355–356, 366 and 374. Pinned
[conversion outputs](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_fpu/convert.expected)
and [arithmetic/sign outputs](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_fpu/arithmetic.expected)
corroborate truncation, saturation and raw sign behavior. `cpu_fpu_convert`
checks literal published vectors, every float exponent, integer powers of two,
all source/destination register pairs, flag preservation, reserved fields, CU1,
delay/annul and original guest RAM results with full-System replay.

## Minimum and maximum selection

MAX.S/MIN.S select and copy an entire operand encoding. Their ordering uses
sign and raw exponent/fraction magnitude, including exponent-zero fractions;
negative zero sorts below positive zero. Thus mixed-zero MAX selects +0 and
MIN selects -0 regardless of source order. No host NaN/infinity conversion or
comparison-instruction zero flushing is used. Current O/U clear; C, I/D,
sticky flags, ACC and other registers are preserved. All source/destination
aliases and writable FPR0 are supported.

Primary instruction pages 363 and 365 specify selection and O/U clearing;
the Core manual, page 163, also specifies every signed-zero MIN/MAX result.
The pinned arithmetic outputs linked above establish mixed-zero selection,
exponent-255 handling and preservation of selected exponent-zero payloads
(for example MIN of 0x00000001 and 1.0 returns 0x00000001). Raw ordering across
all exponent-zero pairs is an implementation inference consistent with these
results, not an exhaustive physical-hardware measurement. The test suite uses
explicit ordered bit patterns, all register triples, each writable flag bit,
CU1 and delay/annul behavior, and original guest RAM/replay expectations. Wider
physical-hardware conformance, including unmeasured operand pairs, remains work.

## Add/subtract and accumulator results

ADD.S/SUB.S write an FPR; ADDA.S/SUBA.S write ACC and require a zero reserved
destination field. Exponent-zero inputs become signed zero, exponent 255 stays
finite, and subtraction flips the right operand sign before addition. Integer
significands retain one additional bit during exponent alignment, normalize,
then truncate to the stored 24-bit precision. Bits below that alignment window
are discarded rather than collected into a sticky rounding bit. This gives
1.0 - 2^-24 = 0x3f7fffff, while 1.0 - 2^-25 remains 1.0 in this model.

An exponent above 255 saturates to signed 0x7fffffff and sets O/SO. A nonzero
result whose normalized exponent is below 1 flushes to signed zero and sets
U/SU. Exact cancellation returns +0 without underflow; adding two negative
zeros returns -0. Every operation replaces current O/U and accumulates sticky
SO/SU, preserving C, I/D and SI/SD. ACC and FPR destinations are independently
preserved as appropriate. There are no arithmetic guest traps or host FP casts.

Primary instruction pages 343–344 and 377–378 specify destinations, flags and
saturation. The Core manual pages 156, 163–165 describe the format, signed zeros
and reduced precision. The published arithmetic result facts linked above are
represented by 72 ADD/SUB vectors tested for both FPR and ACC destinations.
The alignment model additionally follows the original reverse-engineering
[normalization notes](https://wiki.pcsx2.net/PCSX2_Documentation/PS2_VU_%28Vector_Unit%29_Documentation_Part_1)
crediting Nneeve's single-guard-bit finding. That evidence describes VU behavior
and discusses EE similarity; exhaustive independent EE alignment conformance
remains unproven. The new boundary expectations document the model explicitly,
not newly measured hardware results. No external emulator implementation was
imported. Multiplication and multiply-accumulate precision/overflow state remain
separate work.

`cpu_fpu_addsub` covers the published vectors, exact normal and exponent-limit
results, both underflow signs, overflow, cancellation, retained/discarded alignment
boundaries, flag accumulation, register aliases, CU1, reserved fields, delay/annul
and original guest RAM/ACC results with full-System restoration.

## Division

DIV.S uses integer quotient/remainder arithmetic to round a normalized result
to nearest (ties to even), without host floating-point conversion. Unlike the
manual's general rounding description, published physical EE division results
round 1/3 to 0x3eaaaaab and 1/1.5 to 0x3f2aaaab. The result sign is the XOR of
the operand signs; exponent-zero inputs are signed zeros and exponent 255 is
finite. Exponent overflow saturates to signed Fmax and underflow flushes to
signed zero. These exponent cases do not generate O/U for DIV.S.

A zero divisor produces signed Fmax and sets D/SD when the numerator is nonzero,
or I/SI for 0/0. Every division clears current I/D before setting its result
flags, retains sticky SI/SD and leaves O/U, SO/SU, C and ACC unchanged. It does
not raise an arithmetic guest exception. CU1 and ordinary alias/delay/annul
behavior remain enforced; divider latency and interlocks are not modeled yet.

Primary instruction page 357 defines special values and I/D effects. The pinned
[muldiv hardware outputs](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_fpu/muldiv.expected)
cover signed zeros, exponent-zero/255 operands and boundary results. A newer
[physical 90K EE report](https://github.com/PCSX2/pcsx2/issues/14794)
supplies the two nontrivial rounding vectors above, including register aliases
and a delay slot. Nearest-even is the implemented rounding rule; these vectors
establish nearest direction but do not by themselves exhaustively prove every
quotient bit pattern. No external emulator implementation was imported.

`cpu_fpu_divide` checks the published vectors, all 65,025 nonzero exponent pairs
using exact powers of two with both result signs, overflow/underflow, flags,
register aliases, CU1 and delay/annul behavior. An original guest stores 1/3,
a divide-by-zero result and FCR31 to RAM and reproduces full state and traces
from a System snapshot. Broader physical conformance and timing remain work.

## Remaining divider-unit investigation

A separate integer square-root prototype matches the 21 pinned `sqrt.expected`
results, including finite exponent 255. The physical 90K report
[14790](https://github.com/PCSX2/pcsx2/issues/14790) additionally reports +0 and
I/SI for negative zero and negative exponent-zero inputs, contrary to the
instruction manual's negative-zero result wording. These facts should anchor
SQRT.S implementation and flag tests.

RSQRT.S cannot yet be assumed to be DIV.S applied to a stored SQRT.S result.
That composition misses seven of the 39 pinned `rsqrt` rows: 3/sqrt(3) produces
0x3fddb3d8 instead of the recorded 0x3fddb3d7, and maximum-magnitude inputs
produce a final low bit of 3 instead of 2. Rounding the intermediate root upward
also fails other rows. Determine the combined unit's precision/rounding from
stronger evidence before declaring this path conformant; do not special-case
these test operands. This is unfinished investigation, not implemented behavior.

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
