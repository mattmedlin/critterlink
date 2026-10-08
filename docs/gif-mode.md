# GIF_MODE PATH3 mask

GIF_MODE at `0x10003010` supports the M3R mask in bit0. This GIF-owned mask is
independent of the VIF MSKPATH3 mask: PATH3 can acquire packet ownership only
when both are clear. PATH2 remains eligible under either mask.

## Primary evidence

Sony's **EE User's Manual v6.0** documents the register address and word access
on pages21–22, independent masks on page149, the write-only register on page161,
GIF reset on page162, M3R/IMT on page163, and GIF_STAT on page164
([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).
The manual states that masking during transmission takes effect after that
transmission ends. The existing continuous-mode model applies this at the GS
packet's EOP boundary.

## Writes and delivery

A 32-bit write of zero clears M3R; one sets it. Existing direct segment aliases
use the same register. Reads remain unsupported because GIF_MODE is write-only
and its hardware read value is indeterminate. Other widths and set bits reject
without changing state. Rejecting reserved set bits is the emulator's strict
input profile, not a claim that silicon raises the same fault.

Setting M3R preserves queued data and the current packet owner. An active PATH3
packet continues through multiple GIFtags and empty-FIFO gaps until its EOP tag
finishes. Subsequent packets remain queued while either mask is set. Clearing
M3R cannot cancel the VIF mask, and MSKPATH3 cannot cancel M3R. CPU and DMA input
retain their bounded FIFO backpressure while masked.

FLUSHA still waits for queued or requesting PATH3 work under a mask. Software
must clear the relevant masks to permit that work to finish. Mask changes do
not bypass GIF pause or alter GS FINISH events, DMA completion, or BUSDIR.

## Status, reset and replay

GIF_STAT bit0 reports M3R and bit1 reports the independent VIF mask. M3R reports
the programmed value immediately; its delivery effect waits for free packet
arbitration. This is a deterministic status policy rather than a claim about
subcycle hardware timing. Existing queue, owner, direction and FIFO count fields
remain derived from their own state.

GIF_CTRL reset clears M3R to its initial zero along with GIF queues and packet
ownership. It preserves the VIF-origin mask, accepted VIF input and command
state, and GS registers, VRAM, transfers and events. Whole-hardware reset starts
both mask sources clear. Snapshots retain the two masks separately, including a
masked active packet or queued work, so replay preserves delivery order.

## Limits

IMT bit2 remains unsupported. Intermittent PATH3 IMAGE transfer would require
8-qword slice arbitration and suspended decoder state; this mask implementation
does not approximate that behavior. Existing PATH1, GIF decoder and GS format
limits remain in effect. Logical device scheduling is not cycle-accurate GIF
bus timing.
