# VIF UNPACK scalar and full-vector transfers

The supported UNPACK profile covers S-32, S-16, S-8, V4-32, V4-16 and V4-8,
including signed/unsigned expansion and nonzero skipping cycles. It uses the
existing ordered VIF input path for CPU, normal DMA and source-chain transfers.
This is a partial UNPACK implementation, not the full VIF decompressor.

## Primary evidence

Sony's **EE User's Manual v6.0** gives decompression tables on pages89–93,
skipping/filling cycles on page94, masks on pages95–96, addition modes on page97,
STCYCL on page104, UNPACK fields on page123 and CYCLE on page126
([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).

## Formats and padding

| Command | Format | One input element |
| --- | --- | --- |
| 0x60 | S-32 | 32-bit scalar broadcast to X/Y/Z/W |
| 0x61 | S-16 | 16-bit scalar expanded and broadcast |
| 0x62 | S-8 | 8-bit scalar expanded and broadcast |
| 0x6c | V4-32 | Four raw32-bit elements per vector |
| 0x6d | V4-16 | Four expanded16-bit elements per vector |
| 0x6e | V4-8 | Four expanded8-bit elements per vector |

Elements arrive low bits first within each32-bit input word. V4 elements map to
X, Y, Z and W in that order. Immediate USN bit14 selects zero extension when set
and sign extension when clear for8/16-bit elements. It has no numerical effect
for32-bit values, which retain their raw bits. Scalar input repeats the expanded
value into all four destination lanes.

NUM counts output128-bit vectors; zero means256. Payload length rounds up to a
whole32-bit word. Unused high bits in the final scalar word are ignored as
padding. The following word is a new VIF command, even within the same input
qword. No extra128-bit padding is inserted.

## Skipping cycles and addressing

STCYCL sets CL from immediate bits7:0 and WL from bits15:8; NUM is ignored. This
profile accepts `1 <= WL <= CL <= 255`. A cycle writes WL vectors and leaves the
remaining slots through CL untouched. For output index `k`, beginning at zero:

```
destination = ADDR + CL * (k / WL) + (k % WL)
```

For example, ADDR10, WL2, CL4 and NUM5 write vector addresses10,11,14,15,18.
Skipped memory retains its previous contents. Equal WL/CL produces contiguous
output. The complete destination extent is checked before accepting the command;
wrapping outside the1024-vector VU memory profile rejects without partial writes.
The existing synthetic cycle initialization is1/1.

Zero cycle fields and WL>CL filling reject explicitly. Filling requires
supplementary ROW/COL/MASK behavior and must not be approximated by repeating
the last vector. Absolute ADDR bits9:0 are supported; TOPS-relative FLG bit15,
masked commands and other unsupported immediate fields reject.

## Progress and replay

The parser retains format, expansion mode, cycle configuration and exact payload
progress across input boundaries. A32-bit word may produce up to four scalar
vectors; V4-16 and V4-32 may leave a partially written vector. Accepted input is
not replayed after a stall or snapshot restore. Command-shaped payload words
remain data until the declared payload ends.

A new upload waits for the modeled VU program to stop. Existing VIF input
backpressure, DMA completion and abort semantics remain unchanged. Snapshot
validation checks format-specific progress, destination bounds and canonical
inactive state before replacing live state.

## Remaining limits

V2 and V3 formats remain unsupported. Their absent lanes are documented as
indeterminate; this implementation does not invent zero or preserved values for
them. Masked writes, ROW/COL supplementation, addition/difference modes,
TOPS/double-buffer addressing, V4-5, VIF interrupts and exact memory wrapping or
hardware timing remain unfinished. These restrictions also mean the supported
cycle operation is skipping, not filling.
