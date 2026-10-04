# EE floating-point register and control foundation

This implements COP1 register transport and access control, not floating-point
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
read aliases. Arithmetic, comparisons, conversions, COP1 branches and accumulator
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
CFC1/CTC1 pages 353–354, MFC1 page 364 and MTC1 page 371, plus LWC1/SWC1.
Published [FCR outputs](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_fpu/fcr.expected)
and [test definitions](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/ee_fpu/fcr.cpp)
corroborate fixed bits, flag writability, revision value and ignored FCR0 writes.
No external emulator implementation or proprietary program was imported. These
are reference-based tests, not a new physical-hardware run. #17 and #4 remain open.
