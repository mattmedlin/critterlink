# SIO2 diagnostic transport

The IOP can configure and poll two DualShock-style controllers and access two
synthetic raw PS2 memory cards through SIO2 MMIO. This is a deterministic,
single-descriptor PIO profile, not complete SIO2, PADMAN or MCMAN compatibility.

## Registers and transport

All addresses below are physical IOP addresses. FIFO accesses require bytes;
other supported registers require aligned words.

| Address | Supported use |
| --- | --- |
| `1f808200..23c` | SEND3; descriptor 0 only, other entries must remain zero |
| `1f808240..25c` | Interleaved SEND1/SEND2 timing registers |
| `1f808260` | Input FIFO byte write |
| `1f808264` | Output FIFO byte read |
| `1f808268` | CTRL |
| `1f80826c` | RECV1 connection result |
| `1f808270/274` | RECV2/3 reads, diagnostic values `0xf` / zero |
| `1f808280` | Completion status; diagnostic bit 0, write-one-to-clear |

Use `(length << 8) | (length << 18) | route` for SEND3. Routes are `40/41`
for pads and `72/73` for cards on physical ports 1/2. Both length fields are
9 bits and must agree. This follows SDK packet construction, including the
134-byte card transfer, rather than ps2tek's narrower length-field description.

Reset with CTRL `0x0c` or SDK value `0x3bc`, configure the descriptor, stage all
input bytes, then start with `1`, `0x0d` or `0x3bd`. One tick emits one response
byte. Completion clears CTRL bit 0 and sets the diagnostic completion flag.
The entire previous response must be read before starting another transaction,
unless a reset discards it. Input and output each have a 134-byte profile limit.

The completion bit assignment, one-byte tick cadence, zero-filled unspecified
reply positions, packet-level commit and exact reset behavior are explicit
**diagnostic policies**. They are not claims of electrical timing or undocumented
register-bit conformance. SEND1 accepts zero or SDK values `ffc00505`, `ff060505`,
`ff020405`; SEND2 accepts zero, `2000a`, `2012c`, `5ffff`. These are stored for
inspection; timing is not simulated. Other values and descriptor chains fail.

## Controllers

Input button bits mean pressed and are inverted in the wire reply. The four
host axes are copied in wire order into reply bytes 5–8. Input is latched when
CTRL starts the transaction, so scheduled changes cannot tear an in-flight poll.

| Operation | Input bytes | Reply |
| --- | --- | --- |
| Digital poll | `01 42 00 00 00` | `ff 41 5a buttons-lo buttons-hi` |
| Enter config from digital | `01 43 00 01 00` | Current digital poll reply |
| Select analog and lock | `01 44 00 01 03 00 00 00 00` | `ff f3 5a` then six zeros |
| Leave config | `01 43 00 00 00 00 00 00 00` | Config reply |
| Analog poll | `01 42 00 00 00 00 00 00 00` | `ff 73 5a`, buttons, four axes |

Mode selection accepts mode 0/1 and lock 0/3. Entering config from analog uses
nine bytes. Host input has no mode switch, so guest configuration proves the
analog path. Pressure buttons, vibration, multitaps and other commands stop
explicitly; nonzero actuator/padding data is rejected.

## Synthetic card format and transactions

A mounted image contains raw 528-byte pages: 512 data bytes followed by 16 spare
bytes. Sizes must be a multiple of 16 pages, from one erase block to 16,384 pages.
Images are original test data; no filesystem, authentication or ECC generation
is provided. Caller-owned byte vectors are copied/moved into the core, and
`export_card()` returns a copy suitable for host persistence. The core performs
no host file I/O.

| Command | Full packet, including peripheral byte |
| --- | --- |
| Probe | `81 11 00 00` |
| Set erase/write/read page | `81 21/22/23 page-le[4] xor(page) 00 00` |
| Set terminator | `81 27 new 00 00` |
| Get terminator | `81 28 00 00 00` |
| Write N bytes | `81 42 N data[N] xor(data) 00 00` |
| Read N bytes | `81 43 N`, padded with zeros to N+6 bytes |
| End read/write | `81 81 00 00` |
| Erase block | `81 82 00 00` |

N is 1–128; the data packet length is N+6. Read data starts at reply byte 4,
followed by XOR and terminator. The initial terminator is `55`; setting it replies
with the old terminator. Page addressing includes spare bytes. Read/write commands
advance the selected byte cursor. Writes enforce erased-bit programming (1 to 0);
erase sets an aligned 16-page block to `ff`.

A command's changes commit only on its final byte. A reset before that byte drops
the pending command. Missing cards return `ff` with RECV1 `1d100`; present devices
report `1100`. Invalid checksums, protected writes, bounds errors and unsupported
commands produce a sticky descriptive stop without inventing hardware error
codes. Transport reset clears the stop. Earlier completed commands remain saved.

## Snapshots and validation

Snapshots include input/output progress, latched input, controller configuration,
card protocol cursors, mounted image bytes, identity, initial-image FNV hash and
dirty state. An empty instance may adopt snapshot media. An existing mount must
match its identity, initial hash, image size and write policy; a later programmed
version of the same mounted image can be rolled back. Identity mismatches and
malformed snapshots fail before any live state changes. In-flight responses are
recomputed and checked against the saved byte prefix. Mount/eject during active
transfers is rejected.

Unit tests exercise guest analog configuration, latched input changes, partial
packet replay, both ports, exported-image remount/readback, programming and erase,
read-only/absent/checksum/bounds errors, fixed FIFO capacity and atomic restore.
The integrated fixture uses real IOP SB/LBU/SW/LW instructions and scheduled input.

## Sources

Register addresses and protocol baseline come from original
[ps2tek SIO2 research](https://psi-rockin.github.io/ps2tek/#sio2).
Packet sizes, routes, checksums and returned data offsets are cross-checked against
[PS2SDK MCMAN](https://github.com/ps2dev/ps2sdk/blob/master/iop/memorycard/mcman/src/mcsio2.c).
Controller setup and descriptor lengths follow
[PS2SDK pad commands](https://github.com/ps2dev/ps2sdk/blob/master/iop/input/padman/src/sio2Cmds.c)
and [mode selection](https://github.com/ps2dev/ps2sdk/blob/master/iop/input/padman/src/padCmds.c).
[Hardware observations by redpanda4552](https://redpanda4552.github.io/ps2-sio2-docs/)
provide additional connection-status context. No emulator implementation code was
imported.
