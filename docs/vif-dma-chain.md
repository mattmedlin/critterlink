# VIF1 source-chain DMA and tag transport

Channel1 supports RAM source chains with the same eight tag IDs, two-level
CALL/RET stack, fresh/preloaded launch forms and TIE packet boundaries described
in the [GIF chain contract](gif-dma-chain.md). Channel1 additionally supports
TTE=1, forwarding a DMA tag's upper64 bits into the VIF stream. This extends the
existing partial-word VIF delivery path; it does not complete VIF, VU or DMAC.

## Primary evidence

Sony's **EE User's Manual v6.0**, printed pages 45–46, 55–60 and 74–80, specifies
source-chain and channel controls; page86 shows VIF packet layout and page88
specifies MPG alignment ([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).
PS2SDK's pinned
[`packet2_chain_add_dma_tag`](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/ee/packet2/include/packet2_chain.h#L58)
places content at byte8 when TTE is enabled and byte16 otherwise. Its count
calculation excludes the entire tag qword. These establish upper word2 followed
by word3 as VIF stream input; lower words0/1 remain DMA control.

## Registers and delivery

D1_TADR, ASR0 and ASR1 are at 0x10009030, 0x10009040 and 0x10009050. The accepted
profile uses DIR=1, source-chain mode, ASP0–2, optional TTE/TIE, PCE=0 and aligned
RAM addresses. Scratchpad, destination chains, interleave, MFIFO and DMAC stall
control remain unsupported. REFS follows REF only with the accepted STD=0 global
configuration. The manual's concurrent VIF1/TTE and toSPR restriction is not
exercisable because toSPR DMA is absent.

A tag-fetch tick reads and validates all16 bytes before committing its packet
and stack effects. With TTE=0 its upper half is ignored. With TTE=1 the upper two
words are latched in snapshot state and delivered on a subsequent tick, in order.
They do not advance MADR or decrement QWC. Even a zero-QWC packet must finish
its tag words before completing. Preloaded CNT data has no fetched tag to
forward and goes directly through the data path, including with TTE enabled.

Payload qwords retain the existing complete-qword latch and next-word cursor.
A VIF stall preserves every accepted word and retries only the next word. The
last word must be accepted before MADR/QWC advance or a packet completion/TIE
interrupt occurs. DMA completion does not imply VU execution has finished.

The VIF parser survives DMA tag boundaries. Tag upper words can continue an
already active MPG/UNPACK payload; they are not unconditionally commands. MPG
alignment uses the physical lane: command at tag word3 permits payload at the
next qword's word0, while command at word2 is misaligned. Unsupported commands
still stop with a diagnostic under the existing VIF subset.

## Continuation, snapshots and limits

The model accepts the same fresh TAG0/QWC0 and preloaded CNT forms as GIF. TIE
continuation retains TAG, ASP and TTE; TIE itself may change. Changing TTE or
other retained configuration abandons that continuation and requires a supported
fresh form. An interrupted packet's stack effects and upper words are never
applied again on continuation. Terminal IRQ packets end rather than creating a
resumable TIE boundary.

DMAE pauses all chain cursors while VU execution continues independently.
CHCR=0 abort clears pending tag/data transport but preserves already consumed
VIF parser state. Physical D_ENABLEW suspension is not implemented. Out-of-RAM
access sets CIS1/BEIS and clears STR and pending transport; unsupported tag or
VIF capabilities stop diagnostically without fabricating bus-error status.

Snapshot validation covers tag/data phases, active state, stack, address/count,
latched upper words, per-word progress and pending packet outcomes before
changing the destination. A partial VIF payload may legitimately remain after
its DMA packet ends.

One fetch, tag-upper delivery or payload-qword delivery per logical tick is a
bounded scheduling policy, not hardware bus timing. Stack updates at decode,
intra-packet TADR visibility and terminal TADR retention follow the GIF model's
explicit policies. Overflowing CALL ends before either upper tag words or data;
that TTE ordering is a declared choice awaiting hardware observations. Existing
VIF command coverage, VU execution, interrupt/error controls and global DMA
arbitration remain incomplete. Milestone #4 remains open.
