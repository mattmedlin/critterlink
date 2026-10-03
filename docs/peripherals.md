# Peripheral diagnostic primitives

These are host-callable diagnostic building blocks, not complete PS2 devices.
They do not make games' audio or controller code work. No SIO2, IOP, SPU2 MMIO,
DMA, audio clock, or operating-system input/output connection exists in these
standalone primitives. The separate [SPU2 diagnostic](spu.md) adds an IOP-driven
register, sound RAM and voice path.

## Digital pad poll

`DigitalPad` accepts a five-byte `01 42 00 00 00` transaction after `select()`.
Its response is `ff 41 5a LL HH`, with active-low button bits. Host button masks
use the inverse convention (1 = pressed), in PS2 order: Select, L3, R3, Start,
Up, Right, Down, Left, L2, R2, L1, R1, Triangle, Circle, Cross, Square.
Cross plus Start therefore produces `ff 41 5a f7 bf`.

Selection latches the host input as a deliberate diagnostic sampling policy;
this is not a claim about hardware sampling latency. The value state includes
both input masks, selection, and byte position. Restoring it during a packet
reproduces all remaining response bytes. A new selection restarts the packet.
Unsupported commands and transfers outside a selected packet fail explicitly
without advancing state. Analog/pressure/configuration/rumble commands and
physical ACK/timing behavior remain unimplemented.

The protocol and button layout follow the hardware research in
[ps2tek's SIO2 controller sections](https://psi-rockin.github.io/ps2tek/#sio2).
No third-party implementation code was copied.

## Filter-zero ADPCM decode

`decode_adpcm_filter_zero` accepts exactly one 16-byte SPU-format block. It
returns 28 signed PCM samples and the three loop flags; it does not execute
those flags. Supported shift values are 0–12 with filter 0. Other filters,
reserved shifts, and unknown flag bits are rejected. This stateless subset
needs no predictor history. Repeating a block always returns identical output.

The first data byte `87` with shift zero produces 28672, then -32768. A following
`f1` produces 4096, then -4096. The test uses these hand-derived values rather
than a second decoder as an oracle. Fixtures are original literal test data.

Format references are the original reverse-engineering documentation in
[psx-spx SPU samples](https://psx-spx.consoledev.net/ps1/spu/soundprocessingunitspu/#sample-data-brrspu-adpcm)
and [ADPCM decoding](https://psx-spx.consoledev.net/ps1/cdr/cdromformat/#cdrom-xa-audio-adpcm-compression).
This deliberately narrow shared-format primitive is not a validated SPU2 voice
implementation: predictor filters, voice RAM/register access, ADSR, pitch,
interpolation, mixing, reverb, interrupts, and 48 kHz scheduling remain future
work. A PCM diagnostic file generated from it proves block decoding only.
