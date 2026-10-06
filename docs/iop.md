# IOP execution and firmware foundations

`Iop` is an independent 32-bit MIPS-I interpreter with 2 MiB of little-endian RAM.
`start(entry)` preserves the original RAM diagnostic profile and resets CPU state
while retaining RAM. `reset_boot_vector()` enables architectural exception entry,
starts at `0xbfc00000` and selects BEV. The CLI's `--bios` path now starts both EE
and IOP against the supplied immutable image. This is original ROM execution,
not verified PS2 firmware boot. Issue #19 remains open.

## Instruction inventory

| Group | Implemented paths |
| --- | --- |
| Shifts | SLL, SRL, SRA, SLLV, SRLV, SRAV |
| Arithmetic/compare | ADD, ADDU, ADDI, ADDIU, SUB, SUBU, SLT, SLTU, SLTI, SLTIU |
| Logic | AND, OR, XOR, NOR, ANDI, ORI, XORI, LUI |
| HI/LO | MFHI, MTHI, MFLO, MTLO, MULT, MULTU, DIV, DIVU |
| Control flow | BEQ, BNE, BLEZ, BGTZ, BLTZ, BGEZ, BLTZAL, BGEZAL, J, JAL, JR, JALR |
| Loads | LB, LBU, LH, LHU, LW, LWL, LWR |
| Stores | SB, SH, SW, SWL, SWR |
| Exceptions/control | SYSCALL, BREAK, MFC0/MTC0 subset, RFE |

Register zero remains zero. Signed ADD/ADDI/SUB detect overflow even when the
destination is zero; unsigned forms wrap. Multiply/divide update both HI/LO halves
immediately. Signed division truncates toward zero; signed minimum divided by -1
produces low word 0x80000000 and remainder zero without a host overflow. For a zero
divisor, HI receives the dividend. DIV sets LO to 1 for a negative dividend or
0xffffffff otherwise; DIVU always sets LO to 0xffffffff. Neither raises a divide
exception. These IOP-specific results follow published PS2 observations below.
Multiply/divide latency, interlocks and the MFHI/MFLO write-spacing hazard are not
modeled. The operation paths do not establish complete physical CPU conformance.

Taken and untaken branches execute one delay slot. Conditional links write PC+8
regardless of the condition. J/JAL retain the upper bits of PC+4. JALR uses its
specified link register after capturing the original target, including rs=rd
following the published PS2 observation below. Branches in delay slots and
conditional-link/source overlaps still stop explicitly. In architectural mode an unaligned JR/JALR destination
faults on the target fetch, after its delay slot; the diagnostic profile retains
its earlier stop-at-jump behavior.

Loads and MFC0 stage a delayed register write. The following instruction reads old
operands before that write completes; a younger ordinary register write wins.
LWL/LWR merge from a pending value for the same destination, allowing either
adjacent pair order, with a final load delay. Ordinary consecutive loads to the
same destination remain unsupported. SWL/SWR write only their selected bytes,
without peripheral read-modify-write. Merge accesses support RAM and ROM reads,
RAM writes; merge MMIO remains unsupported. Pipeline bypass corner cases still
need independent PS2 hardware checks.

## COP0 and exception behavior

Implemented COP0 registers are BadVAddr (8, read-only), Status (12), Cause (13)
and EPC (14). MTC0 Cause changes software pending bits 8–9 only. Supported Status
bits are the six-bit kernel/user and interrupt-enable stack, interrupt masks
8–15, BEV (22), and CU0 (28). Other Status modes, including cache isolation and
swap, reject explicitly. PRId, debug controls and other registers remain absent.
MFC0 uses the load-delay mechanism; MTC0 writes are immediate in this interpreter,
so its pipeline hazard timing remains unmodeled.

Architectural mode dispatches overflow, syscall, break, data/fetch alignment,
user access to kernel addresses and implemented coprocessor-unusable checks.
An older pending load completes before entry. EPC records the faulting PC or the
branch PC with Cause.BD; entry pushes the Status mode stack and clears current
user/interrupt enable. Nested exceptions overwrite EPC and push again, unlike
EE EXL handling. General vectors are `0x80000080` or `0xbfc00180` with BEV set.
RFE restores the low mode stack and does not branch; the normal return is JR with
RFE in its delay slot. Unknown/unimplemented instructions and devices still stop
rather than masquerading as architectural RI or bus-error exceptions.

Software interrupt bits and an explicit external IP2 line participate in Status
mask/IE arbitration before fetch. The integrated IOP INTC supplies IP2 from
CDVD, SIF DMA, SIO2 and timers; see the register and edge contract below. Entry
preserves pending sources and clears current IE. Acknowledgement must remove the
source. External recognition latency and pipeline timing are not modeled.

## IOP interrupt controller

`IopIntc` implements word accesses to physical I_STAT `1f801070`, I_MASK
`1f801074` and I_CTRL `1f801078`, plus byte access to I_CTRL. I_STAT latches
rising source edges regardless of masks; writing zero acknowledges each selected
bit. I_MASK replaces the enabled-source mask. I_CTRL bit 0 gates the combined
output; reading it returns its previous value and disables the gate. Reads into
register zero retain this side effect, and loads retain the IOP load delay.
Bits 26–31 read zero/ignore writes as a deterministic choice for unspecified bits.
Other subword accesses remain unsupported pending IOP-specific bus evidence;
PS1 partial writes cannot safely be assumed to preserve adjacent lanes.

The integrated sources are CDVD command completion/error (bit 2), the existing
SIF DMA master signal (bit 3), SIO2 transfer completion (bit 17), and qualified
[timer events](iop-timers.md) (bits 4–6 and 14–16). The output
is level-driven into Cause.IP2 (bit 10), with CPU IEc/IM2 as separate gates. Clear
the controller latch and the peripheral's own flag separately. A source held
high does not relatch merely because I_STAT was cleared. Dropping and raising
the source again creates a new edge. Disabling delivery preserves pending status.

Hardware samples before IOP execution, after its bus operation, and after device
service. This retains a falling acknowledgement followed by completion within a
logical tick; resulting guest delivery occurs at the next IOP boundary. No
physical propagation latency or EE/IOP ratio is claimed. Snapshot state includes
source levels as well as status, mask and global enable, so restoration does not
invent a rising edge. Direct host device mutations are sampled on the next
nonzero hardware advance. A zero-tick advance does not sample or dispatch.

References: [ps2tek IOP interrupts](https://psi-rockin.github.io/ps2tek/#iopinterrupts)
for registers/source assignments; [PSX-SPX interrupt observations](https://psx-spx.consoledev.net/ps1/system/interrupts/)
for edge latching, acknowledgements and IP2; and [PS2SDK intrman](https://github.com/ps2dev/ps2sdk/blob/master/iop/system/intrman/src/intrman.c)
for word I_CTRL suspend/resume usage. PSX-SPX describes the IOP as the expanded
PS1 controller; applying its edge behavior is the current platform model, still
requiring independent PS2 hardware conformance. The ps2tek prose labels Cause
with bit 8; the implementation uses bit 10, consistent with the R3000 external
IP2 input and PSX-SPX. No external implementation code was imported.

## Memory, reset and snapshots

RAM occupies physical `0x00000000..0x001fffff`, with KSEG0/KSEG1 aliases. User
access to the kernel half faults in architectural mode. Complete region decode,
RAM mirroring, caches/isolation, memory-control registers and scratchpad remain
unfinished. Non-RAM data accesses use the width-specific `IopBus` adapter.
Unsupported widths/addresses reject before peripheral mutation.

Instruction fetch permits RAM and physical `0x1fc00000..0x1fffffff` ROM only.
`IopBus::fetch32` is distinct from MMIO reads. The integrated adapter reads the
same image owned by Memory for EE ROM access; no second mutable ROM copy exists.
Byte/halfword/word data reads share it. Missing bytes and writes stop explicitly.
`Hardware::advance` receives the ROM span only for the current call; snapshots
contain bytes and execution state, never host pointers.

Boot reset selects Status.BEV and zeroes other implemented fields as a
deterministic choice, preserves RAM, clears pending load/branch/HI/LO and enables
execution. Full silicon reset values and peripheral reset coordination still need
work. IOP remains disabled in a default System until explicitly started; `--bios`
starts it automatically and prints its final PC. Direct ELF diagnostics retain
the disabled IOP default.

The six IOP timers now expose COUNT/MODE/TARGET and guest interrupts through the
peripheral bus. Their [clock, gate and boundary contract](iop-timers.md) distinguishes
functional behavior from unverified hardware timing and conformance.

Snapshots include GPRs, HI/LO, COP0, branch PC, pending load, profile, enable/stop
state and RAM, with the shared ROM in MemoryState. Restore validates sizes,
register zero, Status support, branch progression and load destination before
replacement. Architectural snapshots may hold an unaligned branch target awaiting
its fetch fault. Unsupported host stops preserve execution state; architectural
faults enter the guest and consume a scheduled boundary without retirement.

## Evidence and remaining work

`iop` preserves prior diagnostic checks. `iop_core` adds literal ALU/HI/LO results,
shift positions, branch/link conditions, every merge-byte position and both load
pair orders, precise/nested/delay-slot faults, COP0 load delay, interrupts, ROM
widths, shared EE/IOP execution and immutable/missing-image checks. An original ROM
handler reads EPC, advances it, returns with JR/RFE and stores expected RAM words.
A full-System checkpoint inside its pending MFC0 reproduces subsequent state and
traces even after replacing and restoring the ROM image.

`iop_intc` checks all controller source positions, four independent delivery gates,
I_CTRL side effects including load-to-zero, held-line acknowledgement, edge-history
restore and malformed snapshots. Original IOP guests start a real modeled CDVD
sector read, SIF transfer or SIO2 pad transaction, service the resulting exception,
acknowledge both levels and return through JR/RFE. Pending and in-handler full
System checkpoints reproduce final state and EE traces under split budgets.
A DMA regression acknowledges an old completion on the exact logical tick of a
new completion and checks that the second edge survives, including replay.
These original tests do not establish independent firmware or silicon conformance.

Remaining #19 work includes instruction/exception conformance and
load bypass corner cases, architectural RI/bus errors, cache/reset/control decode,
PRId/debug registers, remaining INTC access widths and source routing, timer conformance/video clocks, DMA channels,
physical clock ratios and in-flight pipeline work. Independent homebrew and real
firmware evidence are also required. No proprietary firmware or external emulator
implementation was imported.

Primary reference: manufacturer-authored [IDT R30xx Family Software Reference
Manual, revision 1.0](https://student.cs.uwaterloo.ca/~cs350/common/r3000-manual.pdf),
chapters 2–4 and appendix A. It supplies the MIPS-I execution/exception foundation;
PS2-specific memory/peripheral controls require additional platform evidence.


## Independent multiply/divide observations

`iop_arithmetic` checks all 108 numeric DIV/DIVU/MULT/MULTU observations in
[ps2autotests IOP muldiv.expected](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/iop/muldiv.expected),
using operands from [muldiv.c](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/iop/muldiv.c)
at revision `97469ffbed8631277b94e28d01dabd702aa97ef3`. Named array inputs use
only their first u32, as in that IOP test. Results are stored as literal numeric
vectors; the emulator does not generate its own expected quotients/products.
The upstream divide probes explicitly emit opcodes to avoid assembler-generated
zero-divisor traps. The repository describes these `.expected` files as results
captured from PS2 hardware; this project did not run new console measurements.

Our original harness runs both diagnostic and architectural profiles, checks full
IOP state preservation and older pending-load operand timing, and checks zero
registers, taken/untaken delay slots and reserved-field rejection. An original
scheduled EE/IOP guest stores zero-divisor HI/LO results to RAM; restoring after
its first divide reproduces full System state and EE traces with split budgets.
This tests observed numerical behavior, not the upstream IRX module, multiply/
divide cycle latency, interruptibility, interlocks or full IOP conformance.

## Independent load-delay observations

`iop_load_delay` checks seven direct LB/LW observations from pinned
[lsudelay.expected](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/iop/lsudelay.expected),
with inputs and instruction ordering established by
[lsudelay.c](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/iop/lsudelay.c).
A younger write replaces the pending load result; an immediate consumer sees
the old value; a consumer after one intervening instruction sees the loaded
value; a branch immediately after LW compares the old operand. Literal original
guest opcodes store each observed result to RAM in both CPU profiles.

Pending-load checkpoints reproduce complete IOP and System state; partitioned
System runs also reproduce EE traces. These replay checks are integration
properties, not additional console observations. The upstream LD probes are
assembler pseudo-operations and are excluded: they do not establish a native
64-bit IOP load. The seven probes do not settle consecutive same-register loads,
all bypass hazards, pipeline timing or general IOP conformance. No production
CPU change was needed to match these observations.

## Observed JALR source/link overlap

The pinned [branchdelay.c](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/iop/branchdelay.c)
`test_jalr_rs_rd_match` uses the same source and destination register;
[branchdelay.expected](https://github.com/unknownbrackets/ps2autotests/blob/97469ffbed8631277b94e28d01dabd702aa97ef3/tests/cpu/iop/branchdelay.expected)
records result 2, reaching the original target. Critterlink previously rejected
this encoding. It now captures that target before writing the link register.
This implements an observed PS2 behavior even though overlapping registers are
not a portable, restartable MIPS programming idiom. It does not claim a rule for
restarting this sequence after a delay-slot exception.

`iop_jump_link` checks the observed path in both CPU profiles and adds original
checks for link values, delay-slot writes, pending loads, register zero and
reserved-field rejection. Saving between jump and slot reproduces the captured
target and full System state under split execution budgets. These additional
checks are integration invariants, not new console measurements.

## IOP-to-EE SBUS request

Word reads/writes of `0x1f801450` implement only the bit1 EE interrupt request
subset used by PS2SDK `sceSifIntrMain`. Assertion reaches EE INTC bit1 and captures
EE timer HOLD0/1; deassertion rearms the edge. This is separate from IOP INTC,
SIF DMA completion and mailbox acknowledgements. See the
[SBUS contract](hardware-plan.md#sbus-capture-and-timer-hold) for pinned source
evidence, the limited control mask/reset assumptions and original dual-CPU tests.
Full SBUS controls and reverse EE-to-IOP requests remain unfinished.
