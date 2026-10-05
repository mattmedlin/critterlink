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
produces low word 0x80000000 and remainder zero without a host overflow. Zero
divisors remain explicit unsupported stops pending IOP-specific hardware results.
Multiply/divide latency, interlocks and the MFHI/MFLO write-spacing hazard are not
modeled. The operation paths do not establish complete physical CPU conformance.

Taken and untaken branches execute one delay slot. Conditional links write PC+8
regardless of the condition. J/JAL retain the upper bits of PC+4. JALR uses its
specified link register. Branches in delay slots and nonrestartable link/source
overlaps stop explicitly. In architectural mode an unaligned JR/JALR destination
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
mask/IE arbitration before fetch. `set_interrupt_line` supplies that line; it is
not yet connected to a complete IOP INTC or peripheral interrupt router. Entry
preserves pending sources and clears current IE. Acknowledgement must remove the
source. External recognition latency and pipeline timing are not modeled.

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

Remaining #19 work includes instruction/exception conformance, zero-divisor and
load bypass corner cases, architectural RI/bus errors, cache/reset/control decode,
PRId/debug registers, full IOP INTC and device routing, timers, DMA channels,
physical clock ratios and in-flight pipeline work. Independent homebrew and real
firmware evidence are also required. No proprietary firmware or external emulator
implementation was imported.

Primary reference: manufacturer-authored [IDT R30xx Family Software Reference
Manual, revision 1.0](https://student.cs.uwaterloo.ca/~cs350/common/r3000-manual.pdf),
chapters 2–4 and appendix A. It supplies the MIPS-I execution/exception foundation;
PS2-specific memory/peripheral controls require additional platform evidence.
