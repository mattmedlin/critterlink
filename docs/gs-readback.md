# GS PSMCT32 guest readback

Readback uses GS output through a distinct reverse VIF1 FIFO, consumed by EE LQ
or normal channel1 reverse DMA into RAM. It does not read the GIF input FIFO.
The supported profile is PSMCT32, whole-qword CPU transfers, and whole remaining
128-byte-aligned DMA transfers.

## Primary evidence

Sony's **EE User's Manual v6.0**, pages23, 26, 42, 143–144 and149, specifies the
FIFO map, port widths, VIF1 bidirectionality and FDR/FQC fields
([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).
Sony's **GS User's Manual v6.0**, pages74–79 and144, describes packing, size
restrictions and BUSDIR ([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/GS_Users_Manual.pdf)).
Pinned PS2SDK [`screenshot.c`](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/ee/debug/src/screenshot.c)
uses FDR, BUSDIR and channel1 CHCR=0x100. Its VIF DIRECT/FLUSHA/MSKPATH3 setup is supported through channel1 DMA
within the [DIRECT profile](vif-direct.md). Its final CPU VIF FIFO unmask write
remains unsupported, so this does not claim that the unmodified SDK function
works. Original guests exercise both GIF and VIF DMA setup paths.

## Setup and transfer

TRXDIR=1 latches source BP/BW/PSM, source origin and rectangle. Destination
parameters and TRXPOS traversal DIR do not affect readback. Pixels are produced
left-to-right, top-to-bottom, four 32-bit values per qword, lower lanes first.
Source memory uses the same independently checked PSMCT32 swizzle as uploads and
copies. Odd rectangle widths are allowed when total pixels are divisible by four.
Unsupported padding, formats and existing coordinate/physical wrap cases reject
before replacing an operation.

| Port | Supported access |
| --- | --- |
| VIF1_STAT 0x10003c00 | 32-bit FDR bit23 and reverse FIFO count FQC bits28:24 |
| GS_BUSDIR 0x12001040 | 64-bit write, bit0 chooses reverse/forward |
| VIF1_FIFO 0x10005000–0x10005ff0 | Aligned 128-bit reverse reads, with existing direct segment aliases |
| D1_CHCR / MADR / QWC | Normal reverse DMA, CHCR=0x100 plus inert TIE/TTE flags, aligned destination and exact remaining count |

FINISH after readback setup does not wait for host consumption: otherwise the
normal FINISH-before-BUSDIR handshake would deadlock. Reading the final pixel
creates no automatic FINISH event. Channel1 DMA completion still generates its
own CIS1 status independently.

The supported switch sequence drains forward input, sets FDR=1, then BUSDIR=1.
Reverse-to-forward switching requires completed production, a drained reverse
FIFO and inactive DMA; clearing FDR before BUSDIR is supported. Active/buffered
transport cannot be silently discarded by changing direction. Forward GIF stores
and forward DMA delivery are rejected while reversed. Privileged GS event ports
remain accessible. These conservative drain checks describe this modeled path,
not every internal hardware pipeline latch. The switch also requires the modeled
VIF payload parser and VU execution to be idle. Reversing an idle bus without a
readback configured is allowed, but consumers reject it and no data is produced.

## FIFO, stalls and DMA

The reverse queue has sixteen qwords, matching the documented FQC range. One
qword is produced per logical tick when both direction controls enable readback
and space exists. Enqueued values remain latched even if source VRAM subsequently
changes. This production schedule is a deterministic policy, not GS bus timing.

An EE read with a valid progressing but empty queue stalls without modifying
registers or consuming data. Hardware continues advancing during the stalled
boundary, allowing a retry to consume exactly once. Wrong-width and unsupported
reads leave the queue untouched. Const memory inspection remains separate from
this destructive device-read path.

Reverse DMA validates the complete destination qword before consuming FIFO data.
An invalid RAM range sets BEIS/CIS1 and clears STR without losing the qword or
partially writing RAM. Subsequent FIFO reads while hardware is diagnostically
stopped are rejected to retain the failed transfer state. DMAE pauses consumption while the producer may fill the
queue and then wait. The initial profile requires nonzero QWC divisible by eight
and matching all remaining output, including already buffered qwords. This is an
explicit restriction; arbitrary partial chunks, padding, reverse source chains,
interleave and scratchpad transfers remain unsupported.

Snapshots retain source progress, direction controls, queue contents/head/count,
DMA state and existing GS events. Validation must accept completed production
with queued data still waiting, while rejecting inconsistent counters, direction
and active-channel combinations before replacing state.

## Limits

VIF1_STAT is a narrow status profile; unimplemented forward pipeline fields are
not claimed as full hardware status. General VIF CPU input, other
pixel formats and physical GS/VIF timing remain unfinished. Readback tests cover
literal raster output, buffer/coordinate layout, odd-width packing, CPU stalls,
DMA backpressure, atomic bus faults, direction guards and original guest RAM
verification with interrupt and full-state replay. This is useful guest readback
within the declared profile, not complete GS or milestone #4 completion.
