# GIF/GS diagnostic slice

`Graphics::submit_qword` consumes one 128-bit word in little-endian word order.
A GIF tag must select PACKED mode, one A+D descriptor, and at most 32767 loops.
PRE can write PRIM. EOP is accepted but has no arbitration effect. One payload
word writes its low 64 bits to the GS address in the next byte. Invalid or
unsupported input throws `std::invalid_argument` without consuming that word.
The caller must stop execution on this error. A truncated stream remains pending;
the caller can inspect `state().remaining` to identify incomplete packets.

The implemented register encodings are PRIM, RGBAQ, XYZ2, XYOFFSET_1, SCISSOR_1,
FRAME_1, PRMODECONT, TEST_1, ZBUF_1 and NOP. Only flat, untextured, context-1
sprites are accepted. FRAME requires base zero, width 64 and PSMCT32, with a
working bitwise write mask. TEST must disable all tests, ZBUF must mask writes,
and PRMODECONT must select PRIM. Integer-aligned coordinates support offset
subtraction, inclusive scissor bounds, and half-open sprite bounds.

The 64-by-64 framebuffer is a diagnostic linear array of `0xAABBGGRR` words,
not emulated GS local memory. There is no display scanout, pixel clock, VRAM
swizzle, depth storage, texture sampling, blending, fractional rasterization,
VU, VIF, GIF path arbitration, or privileged GS register interface. Reset uses
explicit diagnostic defaults (full-surface scissor and disabled tests/depth
writes), not a claim of hardware reset behavior. Q and Z do not affect this
untextured, depth-disabled path. Unsupported formats/registers fail rather than
silently appearing to work. The renderer does not imply PS2 graphics compatibility.

`GraphicsState` includes the framebuffer, current drawing registers, pending
vertex and GIF payload count. Copying it and using validated `restore` supports
mid-packet replay. No host time or floating-point math influences output.
Tests independently check every framebuffer pixel of a rectangle, clipping,
write masks, offset subtraction, malformed packets, and mid-packet restoration.

Register/tag layouts were checked against the primary homebrew SDK definitions:
[PS2SDK GS register definitions](https://ps2dev.github.io/ps2sdk/gs__gp_8h_source.html)
and [PS2SDK GIF tag definitions](https://ps2dev.github.io/ps2sdk/gif__tags_8h_source.html).
All implementation and tests are original; no emulator core code was imported.
This is not yet validated against a physical PS2 capture.
