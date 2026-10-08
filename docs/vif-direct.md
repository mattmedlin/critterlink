# VIF1 DIRECT and GIF packet arbitration

VIF1 forward DMA now accepts DIRECT/DIRECTHL, MSKPATH3 and the modeled
FLUSHE/FLUSH/FLUSHA commands. Separate PATH2 and PATH3 queues feed the existing GS
packet decoder. This is a bounded transport profile, not complete VIF/GIF timing
or support for the unmodified PS2SDK screenshot helper.

## Primary evidence

Sony's **EE User's Manual v6.0** documents alignment on page88, MSKPATH3 on
page109, flush commands on pages111–113, DIRECT/DIRECTHL on pages121–122,
VIF1_STAT on pages143–144, path arbitration on pages148–149, GIFtag EOP on
page151, and GIF_CTRL/GIF_STAT on pages162–164
([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).
The pinned PS2SDK
[`screenshot.c`](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/ee/debug/src/screenshot.c)
corroborates the mask, flush, DIRECT setup and reverse-readback sequence. It also
uses a CPU VIF1 FIFO write to unmask PATH3; that forward CPU input remains
unsupported here.

## Commands and input

| Command | Opcode | Implemented behavior |
| --- | --- | --- |
| MSKPATH3 | 0x06 | Immediate bit15 masks or unmasks future PATH3 selection. Other immediate bits and NUM are ignored. |
| FLUSHE | 0x10 | Waits for modeled VU execution to finish. |
| FLUSH | 0x11 | Waits for VU execution and queued or owned PATH2 work. PATH1 is absent. |
| FLUSHA | 0x13 | Additionally waits for queued or owned PATH3 work and an active channel2 request. |
| DIRECT | 0x50 | Transfers the following immediate-count qwords to PATH2; count zero means 65536. |
| DIRECTHL | 0x51 | Same supported behavior in continuous PATH3 mode. Intermittent IMAGE preemption remains unsupported. |

Flush commands ignore NUM and immediate fields; DIRECT commands ignore NUM.
The command interrupt bit remains unsupported and rejects explicitly. Flushes
synchronize the modeled VU/GIF transport, not asynchronous GS rendering or local
copy completion; use the separate FINISH mechanism for its supported fence.

Commands arrive through normal or source-chain channel1 DMA, including supported
TTE tag words. DIRECT must occupy physical qword word3 so that its following
payload is 128-bit aligned. A DIRECT command in TTE tag word3 is supported.
Malformed alignment rejects rather than inserting padding. Subsequent TTE tag
words interposed inside an unfinished DIRECT payload reject explicitly; ordinary
payload continuation across DMA boundaries retains assembly state.

While a VU upload or DIRECT payload is active, words resembling commands remain
payload. DIRECT assembles four words before enqueueing a qword. If the PATH2
queue is full, the fourth input word is retried; the first three remain retained,
and neither the count nor DMA cursor advances for the rejected word. DMA abort
retains accepted VIF transport state, so software restarting an interrupted
stream must supply its remaining data rather than replaying accepted words.

## Arbitration and backpressure

PATH2 and PATH3 each have a bounded sixteen-qword queue. This queue arrangement
and one-qword-per-logical-tick delivery are deterministic model policies, not a
claim about internal silicon buffering or cycle timing. PATH2 wins when no packet
owns the decoder. Once selected, a path retains ownership through every GIFtag
and empty-queue gap until a tag marked EOP has completed. NLOOP=0 still honors
EOP. A tag boundary without EOP does not permit switching paths.

MSKPATH3 prevents future arbitration to PATH3 without discarding queued data or
interrupting its current packet. PATH2 remains eligible. FLUSHA can consequently
wait indefinitely if software masks already queued PATH3 work and never unmasks
it. Device advancement stays bounded; waiting does not spin inside one host call.
GIF_MODE.M3R and intermittent IMAGE mode are not implemented. DIRECTHL therefore
has no distinct preemption behavior in this supported continuous-mode profile.

Each delivery stages GS decoding before removing the queue entry or changing
ownership. Invalid GIF/GS input retains the offending qword and stops the
hardware diagnostically. GIF_CTRL reset clears both transport queues, packet
ownership and the current GIF payload decoder, while preserving GS registers,
VRAM, transfer/event state and the VIF command/assembly state. The VIF PATH3 mask
also remains owned by VIF. GIF pause stops delivery from both paths.

## Status, readback and snapshots

GIF_STAT exposes VIF mask M3P at bit1, pause PSE at bit3, waiting PATH3/PATH2
requests at bits6/7, modeled output-active OPH at bit9, APATH at bits11:10,
BUSDIR-derived DIR at bit12, and PATH3 queue count FQC at bits28:24. APATH is 0,
2 or 3. Queue request bits identify nonempty paths other than the active owner.
Unimplemented fields remain zero; these derived fields are not cycle-accurate
pipeline observations.

VIF1_STAT derives VPS bits1:0 from command/payload progress and retains VU/GIF
wait reasons in VEW bit2 and VGW bit3. Existing reverse-readback FDR bit23 and
FQC bits28:24 remain available. Forward FIFO occupancy is not fully modeled by
those readback count bits.

BUSDIR reversal requires no pending DIRECT payload, PATH2/PATH3 queued data or
packet owner, in addition to the existing DMA, VU and readback guards. It cannot
silently discard forward work. Reverse operation suppresses forward delivery.
Snapshots preserve queue contents and indices, EOP, owner, mask, DIRECT count,
partial assembly and wait reason. Restore validates queue bounds/unused slots,
owner consistency, assembly bounds and retained waits before replacing state.

## Remaining gaps

Forward CPU VIF1 FIFO writes, PATH1/XGKICK, GIF_MODE masks, intermittent IMAGE
arbitration, VIF interrupts and the full VIF register/command set remain
unsupported. Existing GS packet and pixel-format restrictions still apply.
The SDK-style setup prefix can run through supported DMA, but the complete
unmodified screenshot helper requires the missing CPU VIF input path. This
increment does not complete milestone #4.
