# GIF source-chain DMA

Channel2 accepts a RAM-only source-chain profile alongside its existing normal
DMA path. This is functional packet delivery into the shared GIF FIFO, not
cycle-accurate DMA or complete PS2 DMA support. VIF1 and SIF chains remain absent.

## Evidence and supported tags

The primary specification is Sony's **EE User's Manual v6.0**, printed pages
45–46, 54–60, 62 and 74–80 ([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).
The fresh launch convention is corroborated by PS2SDK
[`dma_channel_send_chain`](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/ee/dma/src/dma.c#L186):
QWC=0, MADR=0, TADR=chain address, TAG=0 and CHCR=0x185 when tag forwarding is off.
This SDK usage is software evidence, not a new hardware measurement.

For tag address T, count Q, address field A, inline data D=T+16 and
following inline address E=D+16*Q:

| ID | Tag | Data source | After the packet |
| --- | --- | --- | --- |
| 0 | REFE | A | End |
| 1 | CNT | D | Fetch E |
| 2 | NEXT | D | Fetch A |
| 3 | REF | A | Fetch D |
| 4 | REFS | A | Fetch D, in the accepted no-stall-control profile |
| 5 | CALL | D | Save E, fetch A |
| 6 | RET | D | Pop saved address, or end if empty |
| 7 | END | D | End |

Two return addresses are supported. A third CALL ends immediately without its
payload and sets channel completion, not bus error. Empty-stack RET transfers
its payload before ending. TAG retains the decoded upper16 control bits. Upper64
bits of a DMA tag are not queued as GIF data with TTE=0. GIF packets may span
multiple DMA tags.

REFS behaves like REF only because the accepted D_CTRL profile has STD=0.
Actual stall control, MFIFO, priority changes, interleave, scratchpad and TTE
forwarding remain rejected. The supported profile requires DIR=1, aligned
lower31-bit RAM addresses, zero reserved tag fields and PCE=0; this is stricter
than every register combination that physical GIF DMA may accept.

## Registers, launch and continuation

D2_CHCR, MADR and QWC retain their existing addresses. TADR, ASR0 and ASR1 are at
0x1000a030, 0x1000a040 and 0x1000a050. ASP in CHCR is the canonical stack depth
0–2. Stopped register writes accept aligned RAM addresses and a 16-bit QWC;
active reprogramming is rejected.

Fresh source-chain launch accepts QWC=0 with TAG=0 or CNT. A documented preload
accepts TAG=CNT with nonzero QWC: MADR's initial packet transfers before the tag
at TADR is fetched. Other arbitrary preloaded TAG combinations are unsupported.

With TIE enabled, IRQ in a nonterminal tag stops after its final data qword,
clears STR, sets CIS2 and exposes the following tag in TADR. The model retains
an explicit fetch phase. Restarting with the retained TAG/ASP and STR set resumes
there exactly once; TIE may change. This precise register-write recipe is a
model policy, not an independently measured restart sequence. Reprogramming
stopped address/count/stack registers discards that continuation and requires
a supported fresh launch. Completed REFE with TAG=0/QWC=0 is indistinguishable
from canonical fresh launch and is accepted as fresh when started again.

D_CTRL.DMAE=0 pauses all chain progress. Writing active CHCR=0 is the existing
model's abort operation: pending chain metadata is discarded, visible address,
count and ASR registers are retained. The physical D_ENABLEW suspension sequence
is not implemented. Existing normal-DMA zero-QWC completion is retained as a
model policy; the manual discourages/prohibits that normal-mode start.

## Scheduling, errors and snapshots

Each logical tick fetches at most one tag or enqueues at most one data qword.
Zero-QWC boundaries finish on the tag-fetch tick, so cyclic empty chains remain
bounded by the caller's tick budget. Tag fetch may proceed while the GIF FIFO is
full because TTE=0 emits no tag data; payload waits without advancing MADR/QWC.
Completion means FIFO acceptance, not subsequent GS rendering.

The stack changes at successful tag decode. TADR remains at the current tag
through its payload, then advances at the packet boundary. Terminal tags retain
the current tag address; popped ASR storage is preserved. These visibility and
ordering choices, including CALL-overflow precedence over IRQ, are deterministic
policies awaiting independent hardware observations.

Out-of-RAM tags/data clear STR, latch CIS2 and BEIS, and stop the diagnostic.
Faulted chain phase becomes idle with pending metadata cleared; visible registers
retain diagnostic context. Unsupported tag capabilities stop before committing
the decoded packet and do not masquerade as hardware bus errors.

Snapshots include TADR, ASR, phase, next tag and pending end/IRQ decisions alongside
the existing GIF FIFO/parser and system state. Restore checks mode, stack depth,
alignment, count, active/phase relationships and pending metadata before changing
the destination. Original tests cover literal tag ordering, nesting, TIE,
backpressure, bounded loops, faults, invalid snapshots and guest render/interrupt
replay. Those tests validate this functional contract, not BIOS boot or broad
game compatibility. Milestone #4 remains open.
