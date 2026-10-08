# GIF REGLIST transport

REGLIST adds compact 64-bit register streams to the existing GIF paths. The
supported destinations are PRIM, RGBAQ and XYZ2, plus the documented no-output
descriptors. Existing GS restrictions still apply: this supports the diagnostic
flat, untextured sprite renderer, not the complete GS register set.

## Primary evidence

Sony's **EE User's Manual v6.0** specifies GIFtag fields and NREG=0 meaning16 on
page151, register descriptors on page152, and REGLIST ordering and exceptions on
page159 ([manufacturer document mirror](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf)).
Packet arbitration remains governed by EOP as described on page149.

| Descriptor | Supported REGLIST action |
| --- | --- |
| 0x0 | Write PRIM directly |
| 0x1 | Write RGBAQ directly |
| 0x5 | Write XYZ2 directly |
| 0xe | No output: A+D is treated as NOP in REGLIST |
| 0xf | No output: NOP |

Other active descriptors reject explicitly. Descriptor0xb is reserved; it is not
another NOP. Unused descriptor nibbles beyond NREG have no effect. [PACKED](gif-packed.md)
uses its own descriptor interpretation and lane conversion.

## Ordering and packet boundaries

FLG=1 selects REGLIST. Payload values are already packed: the lower64 bits of a
qword go to the next descriptor, then the upper64 bits go to the following one.
Descriptors start at the least significant nibble of REGS and repeat after NREG
items for NLOOP repetitions. There is no padding between repetitions, including
when NREG is odd. The total item count is NREG × NLOOP; encoded NREG0 means16.

If that total is odd, the final qword's upper64 bits are discarded. The next
qword begins a new tag. PRE and the tag's PRIM field are ignored in REGLIST;
setting PRE cannot override the current primitive. A zero-loop tag ignores all
fields except EOP, as in the existing GIF decoder.

A loop or tag boundary without EOP cannot release ownership to another path.
PATH2/PATH3 queues and masks apply exactly as for the other supported formats.

## Retained state and failures

The parser retains the descriptor list, decoded count and next descriptor index.
For REGLIST, `remaining` counts64-bit items; for existing PACKED/IMAGE it retains
its qword meaning. The maximum REGLIST count is32767 ×16 =524272 items. Snapshot
validation checks mode-specific bounds, descriptor/index consistency and
canonical inactive metadata. Replay resumes at the same descriptor without
repeating either half of an accepted qword. GIF reset clears the parser metadata
along with its remaining count while preserving the existing GS/VIF reset
contract.

A qword can perform two dependent register writes, including drawing with the
lower half before an invalid upper half is detected. REGLIST payload processing
therefore stages a temporary Graphics copy and commits only when both halves
succeed. The existing Hardware FIFO transaction separately protects dequeue and
packet ownership. This correctness-first implementation temporarily copies
Graphics state twice on that path, including its4MiB VRAM; it is not a claim of
optimized rendering throughput. Removing that cost requires preserving the
same atomic failure behavior across every supported format.

Invalid input cannot retain only the lower half's drawing or register effects.
The hardware diagnostic retains the offending FIFO qword for inspection and
replay. Existing strict GS value checks, unsupported formats and logical-tick
scheduling remain unchanged.
