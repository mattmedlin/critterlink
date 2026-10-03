# Subsystem status and compatibility boundary

Critterlink runs original, directly initialized diagnostic programs through the
implemented device paths. It does **not** boot a PS2 BIOS, run commercial games,
or establish general PS2 homebrew compatibility. The implementation is original;
architecture manuals, SDK interfaces and cited hardware research supply reference
facts. No external emulator core, firmware or commercial media is imported.

This matrix describes implemented behavior, not complete console components.
The linked documents define accepted encodings, register values and fault policies.
An unsupported operation stops explicitly unless a documented device error applies.
The combined fixture in issue #15 is verified across the complete platform matrix.
Milestone #4 is reopened: these diagnostic subsets do not complete general PS2
hardware emulation. The [expanded plan](hardware-plan.md) tracks remaining work.

| Subsystem | Implemented diagnostic behavior | Assumptions and unimplemented modes | Targeted evidence |
| --- | --- | --- | --- |
| Program sources and ELF | Original literal EE/IOP/VU programs; reproducible static ELF32 MIPS fixture; validated segment loading | Fixture builder is not a general assembler/linker; no dynamic linking, firmware loader or arbitrary SDK environment | `elf_loader`, `elf_cli`, `fixture_reproducible`; [homebrew](homebrew.md) |
| EE integer CPU | Word/doubleword arithmetic and shifts, scalar logic/comparisons and conditional moves, both HI/LO multiply/divide pipelines, ordinary/likely/REGIMM branches and jumps, byte/half/word/doubleword loads/stores, unaligned merges, LQ/SQ and trap comparisons, PREF/SYNC, SA transfers/counts, PLZCW, packed logical/immediate shifts, QFSRV, wrapping/saturating add/subtract, comparisons, min/max/absolute/mixed arithmetic and permutations, explicit delay slots | Partial instruction set; remaining SIMD/MMI and FPU unsupported; SA encoding/spacing are functional abstractions; merge MMIO and quadword MMIO other than GIF FIFO writes are unsupported; undefined divide results and multiply/divide timing unverified; no pipeline/cache timing | `cpu_memory`, `cpu_scalar`, `cpu_branch`, `cpu_hilo`, `cpu_trap`, `cpu_merge`, `cpu_quadword`, `cpu_sync`, `cpu_sa`, `cpu_plzcw`, `cpu_packed`, `cpu_packed_arithmetic`, `cpu_packed_permute`; [CPU matrix](cpu-coverage.md) |
| EE COP0 and exceptions | Kernel MFC0/MTC0 subset, INTC/DMAC dispatch, selected synchronous exceptions, EPC/BD, EXL/ERL and ERET | No TLB/privilege implementation, Count/Compare, general COP0 support or hazard timing; BEV ROM vectors have no backing BIOS | `cop0`, `interrupt`; [interrupts](interrupts.md) |
| EE/IOP memory | Separate 32 MiB/2 MiB RAM; little-endian width/alignment handling; explicit direct-segment aliases | EE low RAM is a synthetic identity window; no caches, scratchpads, memory mirroring, BIOS ROM or arbitrary virtual translation | `cpu_memory`, `iop`, `system`; [CPU](cpu-coverage.md), [IOP](iop.md) |
| Scheduler and coordinated system | Stable event ordering, bounded execution, logical ticks, ordered recorded input | One EE boundary advances one bus tick; one enabled IOP instruction per tick. This is not either processor's physical clock ratio | `scheduler`, `hardware`, `system`; [schedule](scheduling.md), [plan](hardware-plan.md) |
| EE timers and INTC | Four counters, divisors 1/16/256, compare/overflow, masks and acknowledgments | No gate, HBlank/VBlank or HOLD. Flag rearm suppression and prescaler reset rules are diagnostic policies awaiting hardware comparison | `hardware`, `interrupt`; [timer profile](hardware-plan.md) |
| EE DMA | Normal channels 1/2/5/6, enable/completion flags, bounded transfers, VIF stalls and SIF FIFO pressure | No chains/tags, scratchpad/interleave transfers, remaining channels, priority arbitration or cycle-level bus simulation | `hardware`, `vif_dma`, `sif`; [plan](hardware-plan.md) |
| GIF | Shared 16-qword CPU/DMA FIFO with stalls, reset/pause/status; PACKED single A+D descriptor, PRE/PRIM, partial packets | No other packet formats, path arbitration or VU XGKICK path; EOP has no arbitration effect | `graphics`, `hardware`, `gif_fifo`; [graphics](graphics.md), [FIFO](gif-fifo.md) |
| GS/reference renderer | Flat context-1 sprites, integer coordinates, scissor/offset/write mask; exact 64×64 RGBA surface | Surface is a linear diagnostic buffer, not GS local memory. No textures, depth, blending, fractional rasterization, privileged registers, scanout or GPU backend | `graphics`, `system`; [graphics](graphics.md) |
| VIF1 | NOP, fixed STCYCL, MPG, UNPACK V4_32, MSCAL, FLUSHE; partial uploads and stalled words | No VIF0, interrupt codes, masks, other unpack formats, double buffering or direct GIF traffic | `vector`, `vif_dma`, `vector_system`; [vector](vector.md) |
| VU1 | Upper NOP/E plus lower IADDIU/LQ/SQ; VI/VF and micro/data memory; E delay pair | No floating-point arithmetic, branches, flags, pipelines, XGKICK, VU0 or EE COP2 macro execution | `vector`, `vector_system`; [vector](vector.md) |
| IOP CPU | Independent 32-bit scalar subset; branch and load delays; byte/halfword/word RAM and MMIO accesses | Explicit start, disabled by default. No firmware boot, coprocessors, IOP interrupt/exception dispatch or complete MIPS-I set; overlapping same-register loads reject | `iop`, `iop_system`; [IOP](iop.md) |
| SIF | Normal bidirectional EE/IOP DMA, bounded queues, mailboxes and flags; guest-computed reply | FIFO capacity/service order and IRQ observation are diagnostic policies. No SIFCMD/SIFRPC HLE, tags, chains or firmware initialization | `sif`, `iop_system`; [SIF](sif.md) |
| SPU2 | Core 0 voices 0/1, manually uploaded RAM, five ADPCM predictors, loops/history, supported ADSR phases, signed stereo mixing, integer pitch and PCM signature | One sample per logical tick; explicit rounding/phase policies. No core 1/other voices, interpolation, fractional pitch, effects, noise, sweeps, audio DMA, SPU interrupts or host playback | `spu`, `spu_system`; [SPU2](spu.md) |
| SIO2 transport | Single-descriptor PIO, byte FIFOs, latched input, partial response state | One byte per tick, packet-level commit and completion-bit assignment are diagnostic policies; no descriptor chains, DMA or full electrical timing | `sio2`, `io_system`; [SIO2](sio2.md) |
| Controllers | Two ports, guest digital/analog configuration, buttons and four axes, recorded input | No pressure-sensitive buttons, actuators, multitaps or full PADMAN behavior. Legacy direct digital-pad endpoint is separate | `peripherals`, `sio2`, `io_system`; [SIO2](sio2.md) |
| Memory cards | Synthetic raw 528-byte pages, addressing/checksums, programming, block erase, readback, export/remount and media identity | No filesystem, authentication or generated ECC. Persistence is explicit host byte export; core opens no files. Writes commit at final packet byte | `sio2`, `io_system`; [cards](sio2.md) |
| CDVD | Read-only synthetic 2048-byte CD sectors, ReadCD parameters, DMA3, status/error/completion, bounded partial transfer | At most eight sectors; 16 bytes per tick. No seek calibration, authentication, ISO filesystem, DVD framing, SCMD, tray/audio commands or complete shared IOP DMAC | `cdvd`, `io_system`; [CDVD](cdvd.md) |
| Snapshots and replay | Explicit CPU/RAM/device/input state, partial transfers, decoded audio/envelope state, framebuffer, card bytes and media identities; validated replacement | In-memory API only, no stable save-file format. Disc must already match; existing card mounts must match identity/policy. Card rollback restores completed diagnostic writes | `system`, `interrupt`, `vector_system`, `iop_system`, `spu_system`, `io_system`; linked subsystem contracts |
| Host presentation and input | Headless framebuffer/PCM data and recorded controller events; CLI diagnostics | No window/GPU renderer, sound-device playback, physical controller discovery, wall-clock synchronization or host backend handles inside snapshots | CLI contracts and deterministic data comparisons; [plan](hardware-plan.md) |

## Completed diagnostic integration evidence

The combined fixture checkpoints active GIF DMA, SPU2 audio and a SIO2 input
transaction at tick95, with a future controller event still pending. Independent
expectations cover every framebuffer pixel, a literal PCM sequence and controller
replies consumed by guest code. Restoration reproduces full CPU/device state and
subsequent outputs, EE instruction traces and per-step IOP PC/register/load-delay
observations. These IOP observations are not a separate instruction-trace API.

All twenty-five suites passed Windows, Linux, macOS ARM64 and macOS x64 in both
Debug and Release. Local ASan/UBSan validation also passed. See
[validation.md](validation.md) for the exact revision and CI evidence, and
[integrated.md](integrated.md) for the fixture contract. This completes issue #15 only. It does not complete milestone #4, establish full
hardware accuracy or demonstrate game compatibility.
