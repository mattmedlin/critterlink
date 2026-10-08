# GS local memory and PSMCT32 uploads

The GS now has 4 MiB of heap-backed local memory. The supported transfer profile
uploads PSMCT32 pixels through BITBLTBUF, TRXPOS, TRXREG, TRXDIR and HWREG, including
GIF IMAGE packets, and bounded nonoverlapping PSMCT32 local copies. The existing fixed 64×64 sprite framebuffer shares this memory;
its diagnostic pixel view is a validated cache of the same contents.

This is a limited format and direction. Textures, depth storage/tests, blending,
scanout, other pixel formats, overlapping copies remain separate work. Guest readback has its own
[PSMCT32 transport profile](gs-readback.md).
A host inspection of VRAM is not guest local-to-host transfer support.

## Primary sources and address layout

Sony's **GS User's Manual v6.0**, printed pages74–77, 101, 111, 132–134, 162–164
and 171, specifies transfer registers, pixel packing and memory layout
([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/GS_Users_Manual.pdf)).
The PSMCT32 block and word tables were transcribed from the page164 diagram.
Sony's **EE User's Manual v6.0**, GIFtag table and IMAGE section, specifies GIF
transport ([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).

A PSMCT32 page is 64×32 pixels (8192 bytes), a block is 8×8 (256 bytes), and a
column is 8×2 (64 bytes). Buffer width BW is in 64-pixel units; base BP is in
256-byte blocks. Addressing adds BP to page/block/column/word offsets, including
carry into another physical page for a non-page-aligned BP. It does not treat
VRAM as a linear image. PSMZ32 has a different block layout and is rejected.

Independent test offsets include (BP0, BW1): (2,0)→16, (0,8)→512,
(16,0)→1024, (32,0)→4096 and (63,31)→8188 bytes. With BP1/BW1,
(56,24)→8192 checks base-pointer carry. Page bijection checks complement these
literal expectations rather than generating expected values with the production
address helper.

## Upload behavior

| Register | Implemented behavior |
| --- | --- |
| BITBLTBUF 0x50 | Retains source/destination parameters; upload uses DBP, DBW and DPSM |
| TRXPOS 0x51 | Retains origins/direction; upload uses destination origin and forward traversal |
| TRXREG 0x52 | Retains rectangle width and height |
| TRXDIR 0x53 | 0 upload, 1 readback, 2 local copy, 3 cancellation |
| HWREG 0x54 | Two PSMCT32 pixels, lower32 then upper32; inactive writes are ignored |

Starting a transfer validates and latches its complete destination rectangle
before changing progress or memory. Later parameter writes affect the next start,
not the active transfer. Restart discards the previous cursor; cancellation
preserves already written pixels. Unsupported starts leave an active transfer
unchanged.

The supported profile requires PSMCT32, DBW1–32, an even nonzero width, nonzero
height, coordinates within 0–2047, a rectangle wholly within the configured
buffer width and physical addresses within 4 MiB. Zero-area, coordinate/buffer
crossing and physical wrap cases are explicitly rejected. These are compatibility
restrictions, not claims that hardware rejects them: the manual describes
coordinate wrapping, while interactions with buffer-width and physical-memory
wrap need further work. Irrelevant source-side format parameters do not constrain
host-to-local uploads. Reserved register bits remain strictly checked.

GIF IMAGE modes2 and3 send each qword's lower64 then upper64 through HWREG. PRE,
PRIM, NREG and REGS are ignored for IMAGE. Zero-NLOOP tags emit nothing and ignore
the other fields, including PRE. PACKED remains limited to one A+D descriptor;
REGLIST remains unsupported. IMAGE data is never reinterpreted as A+D register
addresses. A transfer can span IMAGE packets or mix A+D HWREG with IMAGE input.
Completion is based on rectangle pixels, independent of GIF packet boundaries.
Extra HWREG data after completion is ignored, including IMAGE padding. HWREG input
is also ignored during a local copy.

## Local-to-local copies

TRXDIR=2 latches both PSMCT32 buffer descriptions, origins, rectangle and DIR.
The GS manual's page74 diagram defines the origins as upper-left corners. DIR
chooses traversal order: 0 upper-left, 1 lower-left, 2 upper-right, 3 lower-right.
Reversing traversal changes the first pixel, not the final source-to-destination
mapping. Both source and destination obey the upload profile's format, width,
coordinate and physical-range restrictions.

An exact physical-word intersection check rejects overlapping rectangles before
replacing active state. Different base pointers and coordinates can still alias;
a coordinate-only comparison is insufficient. This is an explicit compatibility
limitation: the inspected manual does not settle internal buffering and collision
results for overlap, and the model does not guess memmove semantics.

One pixel advances per logical hardware tick, after VU execution and before
VIF/GIF delivery. A copy command consumed by GIF begins moving pixels on the next
tick. DMAE and GIF pause do not pause an already started copy. Each step reads
current source VRAM and updates shared destination storage/view; it does not
capture a temporary full source image. These are deterministic scheduling and
live-read policies, not measured GS timing.

Snapshot progress may be odd because copies advance one pixel; uploads still
advance in pairs. Validation rechecks both footprints, nonoverlap, direction and
cursor before replacement. Parameter writes leave the active operation's latched
values intact. A valid new upload/copy or cancellation replaces it; invalid starts
leave it unchanged. Extending restart/cancellation to active local copies is an
explicit functional policy pending hardware evidence about interruption timing.

Completion clears diagnostic active state and retains the final cursor. Without
an explicit FINISH request it creates no completion interrupt. The separate
[FINISH event path](gs-finish.md) can fence an active copy for guest acknowledgement.
Copy tests also verify host VRAM assertions and replay; this does not implement
guest pixel readback.

## Rendering, replay and remaining work

Fixed FRAME base0/width64/PSMCT32 sprites read and write the same swizzled storage
as uploads. Masked rendering preserves uploaded bits; raw memory and the 64×64
view must agree. Snapshot restore rejects a conflicting cache or invalid VRAM
size. It also validates parser mode, transfer configuration and progress before
replacing state. Local memory lives on the heap to avoid multi-megabyte stack
frames in hardware and system snapshots.

Data is consumed through the existing logical GIF FIFO schedule. Rectangle start
validation and existing sprite rasterization execute synchronously; this is not
GS cycle timing. Original tests exercise raw layout, uploads, padding, parser
continuation, start/cancel, atomic errors, shared sprite storage and guest
FIFO/DMA upload with interrupt and full-state replay.

Overlapping local copies need independently established collision behavior.
The separate [guest readback implementation](gs-readback.md) provides BUSDIR,
reverse FIFO and CPU/DMA transport; host VRAM inspection is not its substitute. Other formats,
framebuffer configurations, texture sampling and real scanout remain open.
Milestone #4 is not complete.
