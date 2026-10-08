# GIF PATH3 CPU and DMA FIFO

The EE uses SQ to send packets to GIF_FIFO. CPU stores and normal/source-chain
channel-2 DMA share an ordered, bounded queue of sixteen 128-bit entries. The
consumer supports PACKED A+D drawing/register packets and IMAGE uploads into
[GS local memory](gs-local-memory.md). VIF DIRECT uses a separate PATH2 queue with [EOP arbitration](vif-direct.md).
REGLIST, VU XGKICK and complete GS rendering remain unsupported.

## Addresses and transactions

All qword-aligned addresses from `0x10006000` through `0x10006ff0` address the same
write-only FIFO. Existing kernel direct aliases (`0x90006000` and `0xb0006000`,
including the corresponding window offsets) share the queue. SQ masks its low
four effective-address bits before accessing the bus. Scalar stores, merge stores,
loads including LQ, and other FIFO windows remain explicit unsupported accesses.
The host quadword bus API requires alignment.

Enqueue accepts all four words together or accepts nothing. On a full queue,
Memory raises a retriable `MemoryStall`. CPU stepping records a stalled boundary,
preserves the pending instruction and branch context, and does not retire the
store. System stepping advances devices, then retries on the next boundary.
Instruction budgets include stalls; direct CPU stepping cannot drain a queue by
itself. Interrupt sampling remains before instruction execution, including retries.

DMA observes the same capacity. If full, its address, count and start bit stay
unchanged; it does not read ahead from RAM. After an accepted entry, DMA advances
its address/count and can signal completion before the GS has consumed that entry.
Disabling DMA stops DMA enqueue only, not CPU writes or queued GIF decoding.

## Logical scheduling and registers

A CPU enqueue occurs during its instruction boundary. The following logical tick
runs existing devices, attempts one DMA enqueue, then consumes at most one FIFO
qword. A full queue therefore releases space at the end of that tick; DMA retries
on the next tick. This preserves the former empty-queue DMA diagnostic cadence.
CPU/DMA ordering, service rate and stall latency are explicit emulator policies,
not measurements of physical bus arbitration or clock ratios.

`GIF_CTRL` at `0x10003000` supports PSE (bit 3) and RST (bit 0). PSE pauses decoding
while enqueue can continue until full. RST clears both path queues, ownership and partial GIF tag
payload state; it does not reset GS registers, framebuffer or a pending GS vertex.
The write's PSE bit establishes the post-reset pause state. Resetting GIF does not
cancel active DMA or clear an unrelated hardware stop. Other CTRL bits reject.

`GIF_STAT` at `0x10003020` exposes PATH3 occupancy in FQC (bits28:24),
PSE, waiting PATH3/PATH2 requests, retained packet ownership in APATH/OPH,
VIF's PATH3 mask and BUSDIR direction. See the [DIRECT contract](vif-direct.md)
for exact fields and packet arbitration. GIF_MODE masks/intermittent mode and
diagnostic count/tag registers remain unsupported. Reverse GS pixel transport
uses the separate [VIF1 readback path](gs-readback.md).

An unsupported queued packet stops hardware at that entry. Its FIFO head and
prior graphics state remain intact. Already accepted DMA data is not rolled back.
Snapshots include queue order, head/count, pause state and partial decoder/GS
state. Restore rejects invalid bounds or nonzero unused slots before replacing
live hardware.

## Evidence and references

`gif_fifo` tests guest CPU packet submission, exact framebuffer expectations,
all FIFO address aliases, queue capacity, CPU/DMA backpressure, delayed stores,
DMA completion before decode, paused and partial-packet replay, reset isolation,
unsupported transactions and invalid snapshots. Existing hardware/integrated
diagnostics retain their fixed timing and output oracles.

Primary reference: Sony *EE User's Manual*, version 6.0, printed pages 20 and 24
(FIFO width and register map), 148 (PATH3), and 162–164 (CTRL and STAT)
([manual mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).
The manual establishes the interface and queue capacity; it does not establish
this interpreter's tick scheduling or CPU-full retry timing.
