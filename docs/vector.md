# VIF1 / VU1 diagnostic coverage

This is a deliberately limited, deterministic VU1 transfer and integer-address
execution profile. It does not implement a general vector processor or claim
commercial software compatibility.

## VIF stream

`VectorUnit::submit_word` accepts one 32-bit stream word. A false return means
that word was not consumed and must be retried after a VU tick. Supported codes:

| Code | Supported fields and behavior |
| --- | --- |
| NOP | Exactly `0x00000000` |
| STCYCL | Exactly `0x01000101`, WL=CL=1 |
| MPG | `0x4a`, bounded VU1 micro-memory destination, NUM pairs; zero means 256 |
| UNPACK V4_32 | `0x6c`, absolute 10-bit destination; no flags, masks, or cycle variations; zero NUM means 256 |
| MSCAL | `0x14`, NUM=0, pair address 0..2047; waits while running |
| FLUSHE | Exactly `0x10000000`; waits while running |

MPG and UNPACK wait for an idle VU before consuming their command. MPG consumes
lower then upper words. UNPACK consumes x/y/z/w in order. Partial uploads are
visible in state and retained through snapshots. Transfers crossing the end of
memory are rejected; wraparound behavior is outside this profile. VIF interrupt
bits, double buffering, direct GIF traffic, masks, other unpack formats and
register controls are rejected explicitly. The standalone word parser has no
stream offset; its caller is responsible for MPG payload alignment. Integrated
DMA enforces MPG command placement in word 1 or 3 of a qword so its following
64-bit payload is aligned. No bus timing or FIFO occupancy is modeled.

## Microinstructions

Each logical device tick executes one lower/upper instruction pair. The upper
instruction must be NOP (`0x000002ff`), optionally with E (`0x40000000`). E permits
one subsequent pair before stopping. Other control bits are rejected. Lower
instructions are IADDIU (15-bit unsigned immediate and 16-bit integer result),
LQ, and SQ. LQ/SQ support component masks, signed 11-bit offsets, and local
qword addresses 0..1023. VI0 remains zero and VF0 remains `{0,0,0,1.0}`. Memory
instructions in the E delay slot are rejected. The harmless integer instruction
`IADDIU VI0,VI0,0` supplies neutral lower operations in the fixture.

The implementation commits operations immediately per logical tick. It does not
model VU pipelines, interlocks, hazards, floating-point arithmetic, MAC/status/
clip flags, branch behavior, XGKICK, VU0 or COP2 macro execution. The fixture
inserts neutral instruction pairs around its load/store instead of depending on
this immediate-commit policy for a tightly scheduled program. Unsupported
instructions fail before changing state or advancing the micro PC.

## Fixture and snapshots

The independent fixture uploads integer lane bits `{7,9,11,13}` to qword zero.
A real microprogram computes VI1=1 using IADDIU, loads VF1 from qword zero, and
stores VF1 to qword one, then ends through E and its delay pair. Expected output
is defined directly from those inputs, not from another emulator. This tests
transfer, integer address arithmetic, component storage, completion and replay;
it is not a floating-point conformance test.

`VectorState` stores micro/data memory, VF and VI registers, PC, execution/end
flags and the partial VIF command cursor. Restore validates register-zero and
cursor invariants before replacing state. Unsupported instruction bits may be
stored in micro-memory; validation happens when execution reaches them.

## Sources

Implementation was written from Sony's **EE User's Manual v6.0**, VIF command
reference (STCYCL, FLUSHE, MSCAL, MPG and UNPACK), available in the
[PS2Docs manual collection](https://github.com/ninjadynamics/PS2Docs/blob/main/EE_Users_Manual.pdf),
and Sony's **VU User's Manual v6.0**, upper NOP, lower IADDIU/LQ/SQ and special
register definitions, whose original manual text is available through this
[manual mirror](https://studylib.net/doc/25815876/vuusersmanual.158394566).
No emulator implementation or binary fixture was copied.
