# SPU2 voice diagnostic

This is a bounded core-0, two-voice SPU2 register/RAM processing profile. It does
not claim PS2 waveform or clock conformance. One scheduler tick generates one
logical stereo sample; this is not a model of the physical sample clock.

## Guest interface

Physical IOP addresses are 16-bit aligned accesses relative to `0x1f900000`.
Unsupported addresses, widths, modes and voices fail explicitly.

| Offset | Register/profile |
| --- | --- |
| `000/002/004/006/008/00a` | Voice 0 left/right volume, pitch, ADSR1/2, envelope read |
| `010..01a` | Voice 1 equivalent |
| `188/190` | Dry left/right voice masks, bits 0–1 |
| `198` | MMIX: zero or SDK core-0 default `0xff0` |
| `19a` | ATTR: enable `8000`, unmute `4000`, manual transfer `0010` |
| `1a0/1a4` | Key on/off voice masks |
| `1a8/1aa/1ac` | TSA high/low and staged transfer data |
| `1c0/1c2/1c4/1c6` | Voice 0 start/loop high/low; voice 1 adds 12 bytes |
| `340/342` | ENDX low/high reads |
| `344` | Transfer-active status bit 10 |
| `760/762` | Master left/right direct volume |

Sound RAM has 2 MiB. Addresses use halfword units. Voice block addresses must be
16-byte aligned. Upload at most 32 halfwords through DATA, then write ATTR with
manual-transfer mode. Each tick drains one halfword. Poll STAT before staging
another batch. The entire batch must fit RAM; transfers do not wrap. Upload and
voice address changes while active are rejected. Transfer completion leaves the
manual mode selected until the guest changes ATTR.

Direct volume is signed 15-bit, doubled internally. Pitch is fixed per key-on:
zero, `0x1000` or `0x2000`; a different live pitch is rejected. The integer pitch
selects every sample or every second sample, without Gaussian interpolation.
Pitch zero holds the selected sample while the envelope advances.

## Processing and reproducibility

All five ADPCM predictors retain both histories across blocks and loop jumps.
The predictor uses `(h1*c1 + h2*c2 + 32) >> 6`, added to the sign-extended,
shifted nibble and saturated to 16 bits. Shifts 0–12 and flag bits 0–2 are
accepted. Loop-start captures the block address; loop-end sets ENDX and repeats
when requested. A nonrepeating end turns the diagnostic voice off immediately
after its last sample, rather than modelling inaudible hardware continuation.

ADSR implements linear attack, exponential decay, linear increasing/decreasing
sustain and linear release. Rate shifts through 26 are supported; all-ones
attack/sustain and release-31 freeze. Other slow rates and exponential
attack/sustain/release are rejected. Decay targets are `(SL+1)*2048`, clamped to
32767. Envelope counters and decoded-block cursors are snapshot state.

Diagnostic phase policy updates the envelope before producing a sample; a
reached decay target transitions to sustain on the next tick. Multiplication
uses signed floor shifts: envelope times voice gain, sample times that gain,
then summed stereo voices times master gain, finally saturated. These phase and
rounding choices are explicit deterministic policies pending hardware waveform
comparison. The fixed PCM oracle tests that policy, not silicon equivalence.

Manual transfer runs before voice processing. A bad block records a sticky stop
with voice and block context, without partially committing either voice. A
transfer already performed that tick remains committed. Disabled cores emit
silence and pause voices; muted cores still advance voices. Snapshots include
RAM, transfer staging, decoder histories, envelope counters, last stereo sample,
sample count and a running FNV-1a signature of little-endian stereo PCM. No
unbounded output buffer or per-tick RAM copy is used. Restore validates before
replacing state.

Excluded: core 1, voices 2–23, fractional pitch/interpolation, reverb, noise,
modulation, volume sweeps, DMA/AutoDMA, SPU interrupts, effects/capture RAM writes,
BIOS audio APIs and host audio playback.

## Evidence and tests

The register map and address units follow the primary
[PS2SDK hardware structures](https://github.com/ps2dev/ps2sdk/blob/master/common/include/spu2_mmio_hwport.h).
The staged upload protocol follows
[PS2SDK manual voice transfer](https://github.com/ps2dev/ps2sdk/blob/master/iop/sound/libsd/src/voice.c);
core defaults follow
[PS2SDK initialization](https://github.com/ps2dev/ps2sdk/blob/master/iop/sound/libsd/src/freesd.c).
Envelope arithmetic, predictor and loop behavior use original
[PSX-SPX SPU research](https://psx-spx.consoledev.net/ps1/spu/soundprocessingunitspu/)
as a shared-core baseline, not proof of all SPU2 corner cases.

Tests cover fixed PCM, predictor histories, loop/end, all envelope phases,
counter timing, register-configured two-voice stereo mixing, clipping and signed
volume, unsupported inputs, transfer bounds, and replay of pending upload and
mid-block playback. The system fixture drives the registers through IOP SH/LHU.
