# GIF PACKED descriptor lists

PACKED supports lists of PRIM, RGBAQ, XYZ2, A+D and NOP descriptors through the
existing GIF paths. This expands transport for the diagnostic sprite renderer;
textures, other GS drawing primitives and the full descriptor set remain
unsupported.

## Primary evidence

Sony's **EE User's Manual v6.0** defines GIFtag counts on page151, descriptors on
page152, PACKED ordering and PRIM/RGBAQ extraction on page153, the ST-derived Q
latch on page154, XYZ2/A+D on page155, and NOP on page156
([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).

## Supported conversions

Each descriptor consumes one128-bit payload qword. With input lanes `w0` through
`w3` ordered from low to high:

| Descriptor | Conversion |
| --- | --- |
| 0x0 PRIM | Input bits10:0 select the primitive value. |
| 0x1 RGBAQ | Low bytes of w0/w1/w2/w3 become R/G/B/A; upper32 output bits carry Q. |
| 0x5 XYZ2 | Low16 bits of w0/w1 become X/Y; all32 bits of w2 become Z. |
| 0xe A+D | Lower64 bits are register data; the low byte of w2 selects the register. |
| 0xf NOP | Consume the qword without output. |

Unspecified input bits are ignored for PRIM, RGBAQ and XYZ2. A+D retains the
existing strict profile requiring bits outside its8-bit address to be zero.
Unsupported active descriptors reject; unused nibbles beyond NREG are ignored.

XYZ2 input bit111, or w3 bit15, is ADC. ADC=1 selects XYZ3 and suppresses Drawing
Kick on real hardware. That vertex pipeline is not implemented, so ADC=1 and
direct XYZ3 descriptors reject explicitly. They are not treated as ordinary
XYZ2 writes or as NOP. Existing coordinate, primitive and GS register value
restrictions still apply.

## Counts, Q and tag behavior

NREG encodes1–16 descriptors, with zero meaning16. The least significant REGS
nibble is first; the list repeats NLOOP times. PACKED therefore consumes
NREG × NLOOP qwords, up to524272. There is no per-loop padding or half-qword tail.
The parser retains its descriptor index and remaining qword count across input
stalls and snapshots. EOP releases packet ownership only after all payload ends.

PRE=1 writes the tag's PRIM before payload; PRE=0 leaves it unchanged. Tag fields
and all active descriptors validate before that write. NLOOP=0 ignores everything
except EOP, including PRE. This differs from REGLIST, which always ignores tag
PRE/PRIM and treats descriptor0xe as NOP.

The GIF internal Q value initializes to raw float1.0 (`0x3f800000`) for each tag
and normally changes through PACKED ST. ST remains unsupported, so PACKED RGBAQ
uses1.0 throughout this subset. A+D and REGLIST RGBAQ writes do not modify that
internal latch. The GS RGBAQ Q component is retained separately in snapshots,
although the untextured renderer does not use it. Its synthetic startup zero is
an emulator initialization policy, not a claimed GS power-on value.

## Validation and limits

Snapshots validate mode-specific counts, descriptor sets and indices. PACKED
advances one descriptor per qword, so an odd descriptor index is valid even when
NREG is even; REGLIST has different parity constraints. Completion and GIF reset
clear parser metadata. Existing GIF FIFO staging preserves register/rendering
state, queue contents and ownership when a payload rejects.

General PACKED ST, UV, XYZF, texture and non-kicking vertex transport remain
unfinished. Z is transported to the existing register handler, but this renderer
still performs no depth testing. Logical scheduling is not cycle-accurate GIF
bus timing.
