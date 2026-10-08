# Forward CPU and DMA VIF1 input

The forward VIF1 input queue accepts atomic CPU quadword stores and channel1
DMA input in one ordered stream. This extends the [DIRECT profile](vif-direct.md)
and keeps [reverse readback](gs-readback.md) in a separate queue.

## Interface and evidence

Sony's EE User's Manual v6.0, pages20 and23, specifies quadword FIFO accesses
and the VIF1 window at `0x10005000` through `0x10005ff0`. Pages143–144 specify
FDR and FQC; page88 specifies MPG/DIRECT payload alignment
([manufacturer manual mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).
Pinned PS2SDK [`screenshot.c`](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/ee/debug/src/screenshot.c)
uses a CPU quadword write to unmask PATH3 after readback. Supporting that access
closes a specific transport gap; it does not establish complete SDK runtime or
firmware compatibility.

## Acceptance and ordering

Every aligned address in the window, including existing direct kernel aliases,
addresses the same forward input queue. A CPU store copies all four words or
accepts nothing. A full queue stalls the instruction without retirement; ordinary
device advancement allows retry. Wrong widths or reverse direction reject before
mutation. Commands execute later, so an unsupported queued command produces a
device diagnostic at its retained word rather than undoing an earlier CPU store.

CPU and DMA input follow successful insertion order. During a logical tick, DMA
may append one entry before the consumer attempts the oldest entry. A CPU store
accepted at the preceding instruction boundary therefore precedes that DMA entry.
A stalled producer reserves no queue position. These are deterministic scheduling
policies, not measured physical bus priorities.

The queue has sixteen entries. A normal CPU/DMA entry contains four words; a TTE
entry contains only tag words2/3, with their original lane provenance. Counting
that half-tag as one entry is a conservative bounded policy, not a claim about
silicon half-qword packing. Payload and command interpretation use one parser
regardless of the producer. Software interleaving words during an upload therefore
adds upload data, not an independent command stream.

## DMA completion and stalls

Forward DMA completes delivery when input is accepted by the FIFO. MADR/QWC and
source-chain progress consequently may advance before the VIF command executes.
Completion interrupts and TIE stops do not imply FLUSH or GS completion. Full
input queues preserve unaccepted DMA addresses/counts; accepted data is copied
and survives later RAM mutation.

DMAE pauses the DMA producer, while queued VIF words can continue processing.
DMA abort leaves already accepted input intact. A VU or GIF wait preserves the
head cursor, and replay retains the exact pending word even after DMA has
completed. DIRECT's partial output assembly also survives between input entries.
GIF reset affects GIF queues and ownership, not accepted VIF input.

## Direction, status and replay

FDR selects the modeled forward or reverse queue count in VIF1_STAT.FQC.
Reversing direction requires empty forward ingress along with the existing VU,
DIRECT, GIF and DMA guards. Forward stores are rejected while either FDR or
BUSDIR selects reverse operation.

Snapshots retain ordered input, lane/source provenance, the partially consumed
head and VIF wait state. Validation rejects malformed bounds, noncanonical unused
entries and waits without a retained input word before replacing live state.

## Limits

This remains a functional transport profile. Exact FIFO packing and bus timing,
VIF0, VIF interrupt codes, the full command/register set, PATH1, and intermittent
GIF arbitration remain incomplete. Existing GS pixel-format and reverse-readback
count restrictions still apply. Milestone #4 remains open.
