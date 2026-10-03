# IOP diagnostic CPU

`Iop` is an independent, original MIPS-I scalar interpreter with 2 MiB of
little-endian RAM. Its 32-bit registers are separate from the EE registers.
It defaults to disabled; `start(entry)` explicitly starts a RAM-resident
program and resets CPU state while preserving RAM. This does not boot a BIOS.

The instruction subset is SLL (including NOP), ADDU, ADDIU, ANDI, ORI, LUI,
LH, LHU, LW, SH, SW, BEQ, BNE, J and JR. Arithmetic wraps to 32 bits. Both taken and
untaken branches execute one delay-slot instruction. A branch in that slot
stops explicitly. LW schedules writeback after the next instruction has
read its operands. A younger ALU write to the same register wins; consecutive
loads to the same destination stop explicitly because their hardware-specific
forwarding behavior is outside this profile. Register zero remains zero.

The flat RAM window is `0x00000000..0x001fffff`, also reachable through
KSEG0/KSEG1 aliases. No cache behavior, privilege checks, TLB, RAM mirroring,
BIOS ROM, scratchpad, coprocessor or IOP exception dispatch is implemented.
Aligned 16-bit and 32-bit data accesses are supported. LH sign-extends and
LHU zero-extends; both use the same delayed writeback as LW. SH stores the low
16 bits. IopBus halfword methods reject by default, so peripherals must
explicitly support that access width. Instruction fetch always
requires RAM. Non-RAM data accesses go through an optional `IopBus` adapter,
using the physical address after direct-segment translation. An absent or
rejecting adapter produces a stop rather than dummy data.

State contains all registers, current/next PC, branch delay state, pending
load, enable flag, stop diagnostic and RAM. No callback or host pointer is
stored. Restore validates RAM size, alignment, ordinary PC progression,
register zero, load destination and stop/enable combinations before replacing
live state. A failed instruction latches a diagnostic and leaves CPU execution
unretired, including any pending load. This is a debugger stop policy, not an
architectural R3000 exception entry. An unaligned JR target stops at JR before
executing its delay slot, another explicit diagnostic policy. Peripheral callbacks must reject invalid
operations before changing their own state.

`iop_test.cpp` uses original literal instructions and independently expected
register/memory values. It covers wraparound, logical immediates, aliases,
load delay, load/write conflicts, branches/jumps, mid-load and mid-branch
restore, invalid snapshots, bad alignment, unsupported opcodes and MMIO fetch
rejection. Scheduling and SIF transfer integration are specified separately.

References consulted: the manufacturer-authored
[IDT R30xx Family Software Reference Manual, revision 1.0, chapter 2](https://usermanual.wiki/Document/r3000manual.723589236/html)
for the MIPS-I instruction and delay model. This implementation and its test
programs are original; no external emulator code or firmware is included.
The documented subset and deterministic fault policies do not establish full
R3000 or PS2 IOP compatibility.
