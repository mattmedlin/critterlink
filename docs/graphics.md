# GIF/GS diagnostic slice

`Graphics::submit_qword` consumes one 128-bit word in little-endian word order.
PACKED GIF tags support one A+D descriptor and at most 32767 loops.
IMAGE modes 2/3 feed upload data; zero-length tags ignore all fields except EOP.
PRE can write PRIM for nonempty PACKED tags. Hardware retains PATH2/PATH3
ownership until packet EOP; see [arbitration](vif-direct.md). One PACKED payload
word writes its low 64 bits to the GS address in the next byte. Invalid or
unsupported input throws `std::invalid_argument` without consuming that word.
The caller must stop execution on this error. A truncated stream remains pending;
the caller can inspect `state().remaining` to identify incomplete packets.

The implemented register encodings are PRIM, RGBAQ, XYZ2, XYOFFSET_1, SCISSOR_1,
FRAME_1, PRMODECONT, TEST_1, ZBUF_1, NOP and the upload registers documented in
[GS local memory](gs-local-memory.md). Only flat, untextured, context-1
sprites are accepted. FRAME requires base zero, width 64 and PSMCT32, with a
working bitwise write mask. TEST must disable all tests, ZBUF must mask writes,
and PRMODECONT must select PRIM. Integer-aligned coordinates support offset
subtraction, inclusive scissor bounds, and half-open sprite bounds.

The 64-by-64 view caches `0xAABBGGRR` words from shared 4 MiB swizzled PSMCT32
local memory. Uploads and sprites use that same backing storage. There is no
display scanout, pixel clock, depth storage, texture sampling, blending, fractional rasterization,
VU-to-GIF XGKICK or intermittent GIF arbitration. The privileged interface currently
provides only the [FINISH/CSR/IMR event subset](gs-finish.md). Reset uses
explicit diagnostic defaults (full-surface scissor and disabled tests/depth
writes), not a claim of hardware reset behavior. Q and Z do not affect this
untextured, depth-disabled path. Unsupported formats/registers fail rather than
silently appearing to work. The renderer does not imply PS2 graphics compatibility.

`GraphicsState` includes VRAM, its validated framebuffer view, current drawing
registers, pending vertex, upload cursor and GIF mode/payload count. Copying it and using validated `restore` supports
mid-packet replay. No host time or floating-point math influences output.
Tests independently check every framebuffer pixel of a rectangle, clipping,
write masks, offset subtraction, malformed packets, and mid-packet restoration.

Register/tag layouts were checked against the primary homebrew SDK definitions:
[PS2SDK GS register definitions](https://ps2dev.github.io/ps2sdk/gs__gp_8h_source.html)
and [PS2SDK GIF tag definitions](https://ps2dev.github.io/ps2sdk/gif__tags_8h_source.html).
All implementation and tests are original; no emulator core code was imported.
This is not yet validated against a physical PS2 capture.

## Compact GIF register lists

[REGLIST transport](gif-reglist.md) supports the existing PRIM/RGBAQ/XYZ2
registers and NOP descriptors with full descriptor/loop progression, odd-tail
padding, packet EOP ownership and atomic two-value payload submission. This
expands packet transport while retaining the current sprite-rendering limits.
