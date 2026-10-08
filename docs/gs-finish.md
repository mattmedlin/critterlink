# GS FINISH and privileged event ports

FINISH A+D command0x61 requests a completion event for work already in progress.
The current model exposes its latch through a narrow CSR/IMR interface and
connects the active-low GS interrupt line to EE INTC source0. It does not implement
SIGNAL/LABEL, display events, bus reversal or guest VRAM readback.

## Sources and supported ports

Sony's **GS User's Manual v6.0**, pages77, 94–95, 107, 145–146 and 154, defines
FINISH, CSR and IMR ([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/GS_Users_Manual.pdf)).
Sony's **EE User's Manual v6.0**, pages26 and28, specifies privileged register
widths and INTC edge capture ([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).
Pinned PS2SDK [`draw_finish` / `draw_wait_finish`](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/ee/draw/src/draw.c#L189)
submits FINISH, polls CSR bit1 and writes that bit back. This corroborates positive
event polarity and write-one acknowledgement despite inconsistent prose on GS
manual page95.

| Canonical physical address | Implemented access |
| --- | --- |
| 0x12001000 CSR | 64-bit read of FINISH bit1; write1 to acknowledge, write0 unchanged |
| 0x12001010 IMR | 64-bit write-only event mask bits8–12, 1 means masked |

Existing EE direct segment aliases resolve to these ports. The initial profile
requires aligned LD/SD data accesses; other widths, instruction fetch and other
privileged ports/aliases are explicit unsupported accesses. Existing 32-bit EE
MMIO keeps its separate access rules.

CSR is deliberately synthetic outside the FINISH bit: unimplemented video,
host-interface FIFO and silicon REV/ID fields read zero. Those zeros do not
identify a physical GS revision or describe an actual video/FIFO state. Writes
to read-only bits12–31 are ignored so read-modify-write acknowledgement is usable.
Nonzero unsupported event, FLUSH, RESET and reserved controls are rejected before
mutation. IMR accepts undefined bits as required by software that writes them as
ones, retaining only the five defined mask bits. All masks start set, as documented
by the manual; only FINISH can currently produce an event.

## Completion and interrupt ordering

FINISH with no active transfer latches immediately when GIF consumes the command;
existing sprites execute synchronously. With an active upload or local copy, the
request waits for that operation. GIF continues consuming packets, including
IMAGE data required to finish an upload. Waiting must not deadlock that input.

Completion resolves the pending request. A validated restart or cancellation
resolves the old operation's request before replacing it, so FINISH does not wait
for unrelated later work. Invalid starts preserve the original request and
operation. Waiting on local copies and resolving their cancellation this way are
explicit functional policies; the cited text does not fully specify those cases.
Repeated requests coalesce without stalling. The FINISH latch is always ready in
this profile; no undocumented separate enable/disarm state is inferred. CSR
acknowledgement clears a latched event but does not cancel a pending request.

An unmasked event asserts the GS line low. Its high-to-low transition latches
INTC_STAT bit0; clearing INTC alone does not clear CSR or cause repeated level
relatching. Unmasking a pending GS event creates an edge; masking raises the line.
GS IMR and EE INTC_MASK are separate mask domains. CSR acknowledgement and INTC
acknowledgement are likewise independent. The line is sampled after privileged
writes and GS/GIF progress, and its previous state is serialized and validated.

DMA completion reports delivery into the FIFO and may precede GS FINISH. A local
copy without a FINISH request produces no completion interrupt. Tests distinguish
these events and use original guest LD/SD handlers with exact service counts and
replay of pending, masked and asserted states.

## Remaining work

The full GS privileged register window has additional address aliases and devices.
SIGNAL/LABEL/SIGLBLID, reset/flush behavior, display interrupts, authentic silicon
metadata and host-interface status remain unsupported. BUSDIR and reverse FIFO
transport are needed for readback; a working FINISH acknowledgement alone does
not implement them. Device scheduling still uses logical ticks, not physical GS
clocks. #22 and milestone #4 remain open.
