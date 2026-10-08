# Normal DMA TIE/TTE compatibility

Normal EE GIF, VIF1 forward/reverse and SIF0/1 transfers accept CHCR TIE bit7 and
TTE bit6 as retained configuration bits. They do not change normal payload,
QWC/MADR progress, FIFO stalls or completion: STR clears and CIS latches normally.
No DMA tag is fetched or forwarded because either bit is set in normal mode.

Sony's **EE User's Manual v6.0**, pages55 and74, makes the tag interrupt condition
chain-specific and TTE effective for source-chain transport
([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).
Pinned PS2SDK [`dma_channel_send_normal`](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/ee/dma/src/dma.c#L241)
sets TIE even for normal sends, producing CHCR=0x181 with TTE clear. Rejecting that
value prevented this common SDK launch convention.

The accepted normal mask is 0x1c1, subject to each channel's direction and active
transfer rules. GIF retains its forward-only active profile. VIF1 reverse starts
still require the complete [readback setup and count rules](gs-readback.md).
SIF0 receive and SIF1 send retain their fixed direction requirements, and allow
stopped normal configuration with the matching direction or zero abort. Snapshot
validation accepts the corresponding inactive values after completion, including
0x81/0xc1 for sends and 0x80/0xc0 for receives.

This change does not relax MOD, ASP, TAG, scratchpad or unsupported control bits.
Active transfers still permit only the defined abort write. Chain validators are
unchanged: GIF chain TTE remains unsupported, VIF1 chain TTE forwards upper tag
words, and chain TIE still controls packet interrupts. IOP SIF CHCR has a different
layout and is unaffected. Register retention is a functional model consistent
with the existing completion path, not a new hardware readback measurement.

Tests compare all four inert-flag combinations across the supported channels,
including reverse readback and paired SIF transfers, data order, completion,
snapshots and an original SDK-style normal launch. These checks add software
compatibility without claiming complete DMA or physical timing.
