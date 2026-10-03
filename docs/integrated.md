# Combined audiovisual and input diagnostic

`./build/critterlink --integrated-demo` runs original EE and IOP programs in one
`System`. The EE submits a GIF sprite through normal DMA. The IOP uploads an
original ADPCM block through SPU2 registers, starts its voice and polls a
controller through SIO2. The host loads original program/data bytes and supplies
recorded input; it does not issue peripheral commands or paint expected pixels.

The voice starts at tick 89, the snapshot is taken at tick 95, and execution
finishes at tick 194. Expected CLI output includes `sprite-pixels=12`,
`samples=194`, `pcm-signature=15153771150353129381`, `concurrent=1`, `input=1`
and `replay=identical`.

The checkpoint must contain all three active operations: an incomplete GIF DMA
with a pending sprite vertex, a live SPU2 decoder/envelope, and a partially
returned controller packet. An input change remains scheduled after the
checkpoint and before the packet completes. The first packet retains its
latched buttons; a later guest poll observes the changed buttons.

## Expected outputs and replay

The reference sprite occupies x=2..4 and y=3..6. Those twelve pixels must equal
`0x80402010`; all other pixels in the 64×64 framebuffer must remain zero.
The audio fixture uses the documented filter-1 recurrence and integer envelope
and mixing policy from [SPU2](spu.md). Its expected PCM sequence is literal and
independently calculated, rather than obtained from a first emulator run.
Controller replies are checked against fixed active-low button bytes, including
the difference between the first latched poll and the later poll.
The initial Cross press returns `ff 41 5a ff bf`; the later Start press returns
`ff 41 5a f7 ff`. Guest code records the active-low Cross bit as zero and then
64, proving that it consumed the input response. The event at tick 96 takes
effect before the final byte of the first packet but cannot change its latch.

For audio, each decoded nibble is 7 and the predictor recurrence is
`x[n] = 7 + floor((60*x[n-1] + 32)/64)`. The envelope begins
14336, 28672, 32767; direct voice and master gains are 32766/32768. Forty
literal output samples are followed by one release sample of 51 and silence.
The full-run FNV-1a signature includes 88 leading silent stereo samples and
65 trailing silent samples, encoded as little-endian signed 16-bit channels.

The first continuation and restored continuation compare complete system state.
Tests additionally compare every subsequent stereo PCM sample, framebuffer,
controller output FIFO and read position. EE instruction traces and per-step IOP
PCs, registers, branch state and pending loads are compared separately. These
IOP observations are execution-state evidence, not a newly implemented IOP
instruction trace API. Differently sized execution budgets must reach the same
final state, and zero-budget execution must leave the active checkpoint intact.

Independent expected values establish correctness for the fixture; replay
equality alone would only establish that an implementation repeats itself.
The existing timer, interrupt, SIF, VIF/VU, card and CDVD tests continue to cover
their own guest paths and interactions. The combined fixture does not replace
those targeted diagnostics.

## Scope

This is a headless diagnostic: framebuffer and PCM values are checked in memory.
It does not display a window, play audio through a host sound device, boot a BIOS
or run a commercial game. The clock ratios, sample cadence and protocol timing
remain the explicit policies documented for each device. Refer to the complete
[subsystem status matrix](subsystem-status.md) for unsupported modes and to
[validation evidence](validation.md) for verified platforms and revisions.
