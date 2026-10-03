# SIF diagnostic transport

`Sif` models raw, bidirectional DMA between separate EE and IOP RAM, with shared
mailboxes and flags. Both guest processors must configure their own endpoints.
There is no host callback that supplies a response and no SIFCMD/SIFRPC emulation.

## Supported register profile

All accesses below are aligned 32-bit accesses to physical addresses. The CPUs'
memory buses perform their normal uncached/cached segment alias translation.

| Endpoint | Registers | Supported behavior |
| --- | --- | --- |
| EE SIF0 receive | `1000c000/c010/c020` CHCR/MADR/QWC | Normal receive, start CHCR `100` |
| EE SIF1 send | `1000c400/c410/c420` CHCR/MADR/QWC | Normal send, start CHCR `101` |
| IOP SIF0 send | `1f801520/524/528` MADR/BCR/CHCR | Block send, start CHCR `01000201` |
| IOP SIF1 receive | `1f801530/534/538` MADR/BCR/CHCR | Block receive, start CHCR `01000200` |
| IOP DPCR2 | `1f801570` | Channel 9/10 fields only; enable bits 11/15 |
| IOP DMAC enable | `1f801578` | Bit 0 |
| IOP DICR | `1f8010f4` | Master enable bit 23; computed status bit 31 |
| IOP DICR2 | `1f801574` | Enables 18/19; completion flags 26/27, write-one-to-clear |

The EE's existing D_CTRL gates its endpoints. Completion clears STR and latches
D_STAT channel 5 or 6 through the containing hardware. IOP completion clears
CHCR's transfer bit; DICR2 flags latch when the corresponding channel enable is
set. Master and channel enables gate `iop_irq()`. IOP CPU interrupt dispatch remains
outside this transport: guest code polls CHCR/DICR2 in this diagnostic.

IOP transfers require 32-word blocks with a nonzero 16-bit block count, as used
by PS2SDK's normal-transfer functions. For example, `BCR=00010020` sends or receives
128 bytes. The implementation requires qword-aligned IOP addresses, a stricter
subset than the hardware's word addressability. EE QWC is a 16-bit qword count;
zero QWC completes without transferring data. Counts need not match: an exhausted
producer can leave a receiver waiting, and a completed receiver can leave queued
data and a stalled producer. Software owns the protocol.

MSCOM/SMCOM are at EE `1000f200/210` and IOP `1d000000/010`. EE writes MSCOM;
IOP writes SMCOM. Both can read both. MSFLAG/SMFLAG are at EE `1000f220/230` and
IOP `1d000020/030`. The sender sets bits by writing ones; the receiver acknowledges
those bits by writing ones. Flag writes never imply a DMA transfer or automatically
dispatch a CPU interrupt.

## Deterministic transport policy

Each direction has an eight-qword FIFO. This capacity, one-qword endpoint service
per logical tick, and receiver-before-producer ordering are diagnostic scheduling
policies, **not measured PS2 FIFO depth or bus timing claims**. A tick drains old
SIF0 data to EE RAM, drains old SIF1 data to IOP RAM, produces IOP SIF0 data, then
produces EE SIF1 data. Newly produced data is visible to its receiver on the next
tick. Full FIFOs stall producers; empty FIFOs stall receivers. Disabled endpoints
preserve their progress and queued bytes. DPCR priority fields are stored; this
deterministic service order does not arbitrate according to their values.

The diagnostic IRQ observation is `master_enable && (latched_flags & channel_masks)`.
Clearing a channel mask preserves its completion flag but deasserts its contribution
to that output; reenabling the mask exposes the pending flag again. SDK sources
establish register use but do not verify this particular mask-after-completion
interaction on PS2 silicon. Treat it as an explicit diagnostic contract pending
hardware comparison, not qualified IOP interrupt-controller behavior.

Snapshots include both FIFOs, every DMA register, progress within an IOP block,
pending completion events, mailboxes, flags, masks and the explicit diagnostic
stop. The containing machine also snapshots both RAMs and CPU states. Restoration
validates field ranges and channel modes before replacing transport state.

An invalid source or destination range stops the diagnostic before that qword is
copied or removed from a FIFO; it does not pretend to implement the hardware's
bus-error recovery. Work by an earlier endpoint in the same tick remains committed.
Unsupported registers, chain modes, tags, non-32-word IOP blocks, SIF control and
boot initialization, scratchpad transfers, tag interrupts, and changing active DMA
registers fail explicitly. Writing zero to CHCR stops that endpoint; it does not
flush transport FIFOs. Stopping an IOP endpoint discards its private partial-block
cursor while preserving MADR and BCR; software must reprogram a new block transfer,
not treat cancellation as pause. Clearing DMAC/channel enable instead pauses with
the partial-block cursor preserved. This is a directly initialized transport profile, not a
BIOS boot or arbitrary SDK initialization implementation.

## Sources and validation

The containing `Hardware` owns the IOP and transport. On each logical tick,
timers advance, one enabled IOP instruction runs, SIF transfers run, then VU,
VIF1 DMA and GIF DMA progress. The IOP-to-EE instruction ratio is a deterministic
1:1 diagnostic policy, not calibrated console timing. IOP stops halt this
coordinated run with an explicit diagnostic. The IOP defaults to disabled.
`Memory::iop()` permits fixture loading and explicit start; normal guest access
to SIF uses the CPUs' memory instructions. EE RAM supplied to `Hardware::advance`
is mutable because receive DMA writes it.

`--iop-demo` installs original EE and IOP programs. The EE sends 128 bytes whose
first words are 7, 9, 11, 13; the IOP computes 40, writes SMCOM and sends a response
block through DMA. The EE polls SMCOM and its RAM receives the response. A snapshot
at tick31 has five queued qwords and an IOP load awaiting writeback. Whole-run and
single-step replay reach the same complete state and EE trace at tick223.
The host installs inputs/programs but does not calculate or supply the response.

Original implementation; no emulator source was imported.

- Sony *EE User's Manual*, version 6.0, DMAC sections 5.1 and 5.5:
  [manual mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf).
  Establishes channel directions, normal transfer mode, QWC and STR.
- PS2SDK [IOP register definitions](https://github.com/ps2dev/ps2sdk/blob/master/common/include/iop_regs.h)
  and [EE register definitions](https://github.com/ps2dev/ps2sdk/blob/master/common/include/ee_regs.h)
  establish addresses.
- PS2SDK [SIF MMIO layout](https://github.com/ps2dev/ps2sdk/blob/master/common/include/sif_mmio_hwport.h)
  establishes shared-register offsets and the two CPU aliases.
- PS2SDK [SIF manager](https://ps2dev.github.io/ps2sdk/sifman_8c_source.html),
  `sceSifDma0Transfer` and `sceSifDma1Transfer`, establish the normal-transfer
  CHCR values and block format; its mailbox/flag accessors establish the API direction.
- PS2SDK [IOP control](https://ps2dev.github.io/ps2sdk/iopcontrol_8c_source.html)
  resets individual SMFLAG status bits from EE before waiting for IOP initialization.
- PS2SDK [interrupt manager](https://ps2dev.github.io/ps2sdk/intrman_8c_source.html),
  `EnableIntr` and `dma_interrupt_handler`, establish channel masks, master enable,
  flag positions and acknowledgments.

`sif_test.cpp` covers exact bidirectional bytes, mailboxes, flag acknowledgments,
FIFO pressure, disabled endpoints, mismatched progress, zero-length EE transfer,
completion masks, mid-transfer replay, bounds errors, unsupported operations and
transactional rejection of malformed snapshots.
