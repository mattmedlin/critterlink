# COP0 and guest exception dispatch

Issue #10 adds a kernel-only diagnostic exception path to the scalar CPU.
The later [MMU extension](mmu.md) adds TLB/privilege behavior; full COP0 and BIOS
boot remain incomplete.

## Register and entry contract

MFC0 reads BadVAddr (8), Status (12), Cause (13), EPC (14), ErrorEPC (30),
and the [MMU registers](mmu.md),
sign-extending the word into the low scalar lane. MTC0 writes Status, EPC, ErrorEPC and writable MMU registers. Cause and BadVAddr are read-only in this subset. Noncanonical encodings
and unsupported registers fail explicitly. Supported Status bits are IE (0),
EXL (1), ERL (2), KSU (4:3), IM0/IM1 (10/11), EIE (16), BEV (22), CU0 (28),
and CU1 (29). Writes and snapshots with other Status bits or reserved KSU=3 are
rejected. Count/Compare,
performance interrupts, EI/DI and other COP0 operations remain unsupported.
Synthetic `reset(entry)` starts with Status zero. `reset_boot_vector()` sets
BEV/ERL and enters boot ROM, but does not initialize the unimplemented reset
registers or IOP; see [memory/reset](memory.md).
MTC0 effects are immediately visible at the next boundary in this interpreter;
the EE pipeline's COP0 hazards and SYNC.P requirements are not modeled.

At each CPU step, the current INTC and DMAC lines update Cause bits 10 and 11.
Those pending bits remain observable while masked. Delivery requires IE and EIE,
the matching Status mask, and both EXL and ERL clear. An interrupt enters before
the interrupted instruction executes; that instruction does not retire.
Masking does not acknowledge devices. Handlers must clear the relevant device
status, otherwise the interrupt becomes eligible again after ERET.

| Entry | BEV clear | BEV set |
| --- | --- | --- |
| Interrupt (ExcCode 0) | `0x80000200` | `0xbfc00400` |
| General exception | `0x80000180` | `0xbfc00380` |

COP1 unusable entry uses code 11 and Cause.CE=1; see [FPU](fpu.md).

Overflow, SYSCALL, BREAK, trap comparisons and alignment errors dispatch general
exceptions with codes 12, 8, 9, 13 and 4/5 respectively. Entry sets EXL and updates ExcCode. When EXL
was clear, EPC saves the current PC, or the branch PC with Cause.BD set if the
interrupted/faulting instruction was in a delay slot. This applies to untaken
branches too. Entry discards the pending branch. If EXL was already set, EPC and
BD are preserved while ExcCode and, for address errors, BadVAddr update.

ERET immediately returns through ErrorEPC and clears ERL when ERL is set;
otherwise it returns through EPC and clears EXL. It has no delay slot. ERET in a
delay slot is rejected. Returning to a branch saved with BD restarts the branch
and its delay slot; a handler skipping a fault must deliberately adjust EPC.
BEV vectors execute from loaded boot ROM. If the required ROM bytes are absent,
fetching a BEV handler stops as unsupported access. An original ROM guest tests
syscall entry and handler execution; this does not establish real firmware boot.

TLB misses/invalid/modified and privilege faults dispatch guest exceptions in
architectural mode; see [MMU](mmu.md). Unimplemented instructions/devices and
undefined TLB configurations still produce sticky host stops. Ordinary
guest exceptions produce trace entries with an exception code and no host stop.

## Budgets, timing and restoration

Execution budgets count CPU boundaries: each instruction attempt or interrupt
entry consumes one step, including synchronous exception dispatch. `retired`
counts only successfully completed instructions. This bounds even a handler
that immediately faults again. In `System`, a retired instruction or exception
entry or FIFO stall advances one logical device tick; host stops advance none. This is a
deterministic diagnostic timing policy, not EE cycle timing. Direct `Cpu::run`
samples existing device lines but does not advance devices. Zero budget is a
complete no-op.

All COP0 fields, handler PC, pending branch, device flags, clock and RAM are part
of the existing snapshot. Restoring during a handler reproduces the subsequent
state and trace. Devices continue advancing while EXL suppresses reentry.

## Guest diagnostic

```sh
./build/critterlink --interrupt-demo
```

The original guest fixture configures the timer and normal GIF DMA transfer,
enables IE/EIE and both masks with MTC0, then waits. Its handler reads Cause,
disables/acknowledges the timer, acknowledges DMA, increments separate service
counters, and executes ERET. It restores a snapshot taken inside the handler
and compares complete state and traces against execution in single-step chunks.
The independent expected result is 128 ticks, one service of each source,
12 sprite pixels, both sources acknowledged, a return to the guest loop, and
identical replay. No host callback services an interrupt on the guest's behalf.

References: Sony *EE Core User's Manual*, version 6.0, pp. 73–76, 90–91 and 99
([manual](https://docs.alexrp.com/mips/ee.pdf)), and *EE Core Instruction Set
Manual*, MFC0/MTC0/ERET ([manual](https://docs.alexrp.com/mips/ee_insns.pdf)).
The Core manual's Cause table reverses the external interrupt labels relative
to its Status narrative; this implementation follows the latter's Int0→bit10,
Int1→bit11 mapping. These tests establish the stated subset, not complete
processor conformance or pipeline timing.
