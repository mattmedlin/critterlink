# CDVD read-only diagnostic subset

`Cdvd` mounts up to eight original 2048-byte sectors. Mounting copies bytes into
immutable device media; there is no writable disc command or host file handle.
An empty mount removes the media. Mounting during a command or armed DMA is
rejected. These synthetic sectors are not a bootable disc or a commercial game.

The guest protocol follows PS2SDK's direct CDVD register and DMA3 setup:

| Physical register | Width | Supported use |
| --- | --- | --- |
| `1f402004` | byte | N-command; ReadCD `06` |
| `1f402005` | byte | Write parameters; read ready `40` / busy `80` |
| `1f402006` | byte | Write transfer format `80`; read error |
| `1f402008` | byte | Completion interrupt `02`, write-one-to-clear |
| `1f40200a` | byte | Read `06`, pause `0a`, empty-drive stop `00` |
| `1f40200f` | byte | Mounted CD profile `12`, empty `00` |
| `1f8010b0/b4/b8` | word | DMA3 MADR/BCR/CHCR |
| `1f8010f0` | word | DMA3 priority nibble; enable bit `8000` |

ReadCD receives eleven parameter bytes: little-endian sector number, little-endian
sector count, retry count zero, spindle value one, and 2048-byte data pattern zero.
One sector uses BCR `00100020` and CHCR `41000200`. Program DMA, enqueue parameters,
then issue ReadCD. Completion clears DMA's start bit, decrements BCR's block count,
and raises the CDVD completion flag. The guest can poll or acknowledge that flag;
IOP interrupt dispatch is not implemented.

The API and register constants were checked against original SDK code:
[PS2SDK cdvdman.c](https://ps2dev.github.io/ps2sdk/cdvdman_8c_source.html)
(`sceCdRead0_Rty`, `cdvdman_setdma3`, `cdvdman_send_ncmd`, interrupt handler) and
[SDK drive/error enums](https://ps2dev.github.io/ps2sdk/libcdvd-common_8h_source.html).
No SDK implementation or emulator code is copied into this device.

The following are explicitly diagnostic policies: a transfer advances sixteen
bytes per logical system tick, there is no seek/spin-up latency, only the listed
format and speed are accepted, and the error remains latched until another
command. This device models channel3's enable nibble without a complete shared
IOP DMAC controller or interrupt controller. It does not claim calibrated drive
timing, full DMA arbitration, disc authentication, ISO filesystem access, DVD
sector framing, audio playback, tray commands or an SCMD processor.

Malformed parameters return error `22`, absent media returns `12`, and reads past
the mounted extent return `32`. Unimplemented commands/registers and unsupported
DMA modes reject explicitly. A mismatched DMA length or destination outside RAM
latches a diagnostic stop before the next write; the full remaining destination
is checked before the first transfer. Clearing channel enable or CHCR start pauses progress. Re-arming CHCR resumes
the same cursor and BCR; MADR/BCR cannot be replaced while a read is pending.
A canceled DMA is therefore a resumable pause, not a drive-command abort.
There is no invented sector-data register: successful reads reach IOP RAM only
through DMA3.

Snapshots preserve queued parameters, command/error/interrupt state, DMA registers
and partial byte progress. Media identity uses FNV-1a plus exact byte equality;
a hash collision cannot substitute another disc. Restore requires the same media
already mounted, including when restoring into a fresh device. It validates the
snapshot before replacing live state. Mount the original media explicitly first.

Tests use generated original sectors with an independently specified byte pattern.
They cover every transferred byte and adjacent sentinels, DMA pause, mid-sector
restore/replay, command errors, overflow, invalid snapshots, wrong media, absent
media, unsupported commands, and out-of-range DMA without partial writes.
