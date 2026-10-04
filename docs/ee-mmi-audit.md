# EE MMI decoder inventory and conformance audit

Audit date: 2026-10-04. Scope: primary opcode 0x1c (MMI), its four subgroups,
and the signed-division overflow gap found during the review. Source: Sony's
[EE instruction manual v6](https://docs.alexrp.com/mips/ee_insns.pdf), printed
pages 399–403 for the inventory and individual instruction pages for operands.

## Result

All 99 named instruction entries under opcode 0x1c have decoder paths. Counting
the five PMFHL formats separately gives 103 instruction forms: 78 subgroup
operations, 13 scalar/core-specific forms, six immediate shifts, five PMFHL
formats and PMTHL.LW. This is **decoder coverage**, not full MMI conformance,
cycle accuracy or game compatibility. #17 and #4 remain open.

The new `cpu_mmi_audit` suite exercises all 2,048 function/subopcode pairs.
With canonical register fields, legal operand values and a valid SA token,
257 pairs retire: the 78 subgroup operations, 13 scalar/core-specific pairs,
six formatted transfers and 160 legal immediate-shift/count pairs. The other
1,791 pairs stop atomically. They include reserved slots/formats, nonzero
reserved scalar shift fields and manual-undefined oversized halfword right
shifts. This sweep checks decode/retirement, not exhaustive arithmetic outputs;
the existing focused suites supply arithmetic, routing and guest expectations.

## MMI subgroup matrix

Column headings give the low six-bit function. Rows give bits 10–6. A dash
means reserved in the primary table. Counts: MMI0 25, MMI1 18, MMI2 22, MMI3 13.

| Subopcode | MMI0 (0x08) | MMI1 (0x28) | MMI2 (0x09) | MMI3 (0x29) |
| --- | --- | --- | --- | --- |
| 0x00 | PADDW | — | PMADDW | PMADDUW |
| 0x01 | PSUBW | PABSW | — | — |
| 0x02 | PCGTW | PCEQW | PSLLVW | — |
| 0x03 | PMAXW | PMINW | PSRLVW | PSRAVW |
| 0x04 | PADDH | PADSBH | PMSUBW | — |
| 0x05 | PSUBH | PABSH | — | — |
| 0x06 | PCGTH | PCEQH | — | — |
| 0x07 | PMAXH | PMINH | — | — |
| 0x08 | PADDB | — | PMFHI | PMTHI |
| 0x09 | PSUBB | — | PMFLO | PMTLO |
| 0x0a | PCGTB | PCEQB | PINTH | PINTEH |
| 0x0b | — | — | — | — |
| 0x0c | — | — | PMULTW | PMULTUW |
| 0x0d | — | — | PDIVW | PDIVUW |
| 0x0e | — | — | PCPYLD | PCPYUD |
| 0x0f | — | — | — | — |
| 0x10 | PADDSW | PADDUW | PMADDH | — |
| 0x11 | PSUBSW | PSUBUW | PHMADH | — |
| 0x12 | PEXTLW | PEXTUW | PAND | POR |
| 0x13 | PPACW | — | PXOR | PNOR |
| 0x14 | PADDSH | PADDUH | PMSUBH | — |
| 0x15 | PSUBSH | PSUBUH | PHMSBH | — |
| 0x16 | PEXTLH | PEXTUH | — | — |
| 0x17 | PPACH | — | — | — |
| 0x18 | PADDSB | PADDUB | — | — |
| 0x19 | PSUBSB | PSUBUB | — | — |
| 0x1a | PEXTLB | PEXTUB | PEXEH | PEXCH |
| 0x1b | PPACB | QFSRV | PREVH | PCPYH |
| 0x1c | — | — | PMULTH | — |
| 0x1d | — | — | PDIVBW | — |
| 0x1e | PEXT5 | — | PEXEW | PEXCW |
| 0x1f | PPAC5 | — | PROT3W | — |

## Direct MMI function entries

All unspecified function values outside the subgroup selectors are reserved.

| Function | Instruction | Field restrictions |
| --- | --- | --- |
| 0x00 / 0x01 | MADD / MADDU | bits 10–6 zero |
| 0x04 | PLZCW | rt and bits 10–6 zero |
| 0x10 / 0x12 | MFHI1 / MFLO1 | rs, rt and bits 10–6 zero |
| 0x11 / 0x13 | MTHI1 / MTLO1 | rt, rd and bits 10–6 zero |
| 0x18 / 0x19 | MULT1 / MULTU1 | bits 10–6 zero |
| 0x1a / 0x1b | DIV1 / DIVU1 | rd and bits 10–6 zero |
| 0x20 / 0x21 | MADD1 / MADDU1 | bits 10–6 zero |
| 0x30 | PMFHL.LW/UW/SLW/LH/SH | rs/rt zero; formats 0–4 |
| 0x31 | PMTHL.LW | rt/rd zero; format 0 |
| 0x34 / 0x36 / 0x37 | PSLLH / PSRLH / PSRAH | rs zero; left masks count, right requires count <16 |
| 0x3c / 0x3e / 0x3f | PSLLW / PSRLW / PSRAW | rs zero; all five-bit counts |

For subgroup moves, unary operations and divides, the per-instruction reserved
fields are tested by the corresponding focused suites. Both the inventory sweep
and those suites are required; neither substitutes for the other.

## Conformance gap fixed during this audit

DIV, DIV1 and PDIVW previously stopped on signed minimum word divided by -1.
The manual explicitly specifies quotient 0x80000000 and remainder zero on
printed pages 51, 138 and 197. Their paths now compute safely using signed
64-bit intermediates and sign-extend the low word into the destination HI/LO
half. No guest overflow exception occurs. Existing PDIVBW already implements
its equivalent specified result.

Tests cover scalar pipeline isolation, each packed lane and both lanes together,
complete preserved CPU state, taken/untaken delay slots, likely annulment and
an original guest that saves results from both scalar pipelines and packed
division. Full-System restoration reproduces subsequent traces and RAM.

## Remaining limits and ownership

- **#17:** scalar/packed-word zero divisors and noncanonical word
  multiply/divide sources remain explicit stops. PDIVBW zero-divisor behavior
  is supported using the published hardware evidence linked in CPU coverage.
- **#17:** SA uses an internal bit-count token. Physical MFSA/MTSA encoding and
  arbitrary hardware token compatibility remain unverified; QFSRV accepts only
  supported tokens. This is not a missing MMI opcode.
- **#17:** undefined-input cases and manual inconsistencies need broader
  hardware conformance evidence. Horizontal HI/LO upper words use a documented
  inference from published results, not a universal hardware guarantee.
- **#27:** operations commit synchronously. Latencies, interlocks, dual issue,
  pipeline spacing and cycle accuracy remain unmodeled.
- **#17/#18:** COP1, architectural control, cache/TLB/reset/BIOS behavior remain
  separate major work. SA SPECIAL/REGIMM instructions and LQ/SQ are outside
  primary opcode 0x1c and retain their own documented limits.

See [CPU coverage](cpu-coverage.md) and [integer audit](ee-integer-audit.md).
The count above must not be used to close the broader hardware milestone.
