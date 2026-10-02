# SPDX-License-Identifier: MIT
# Bare-metal EE scalar fixture. Literal instruction words avoid a proprietary
# assembler/SDK dependency. build.py packs these sections into a static ELF.
.section .text
.globl _start
_start:
.word 0x3c060010 # lui   r6,0x0010
.word 0x34c61000 # ori   r6,r6,0x1000       data base = 0x00101000
.word 0x24010005 # addiu r1,r0,5
.word 0x24020000 # addiu r2,r0,0
.word 0x00411021 # addu  r2,r2,r1          loop at 0x00100010
.word 0x2421ffff # addiu r1,r1,-1
.word 0x1420fffd # bne   r1,r0,loop
.word 0x24630001 # addiu r3,r3,1           delay slot
.word 0xacc20014 # sw    r2,20(r6)         sum = 15
.word 0xacc30018 # sw    r3,24(r6)         loop count = 5
.word 0x8cc70000 # lw    r7,0(r6)          initialized data
.word 0xacc7001c # sw    r7,28(r6)         copy = 0x12345678
.word 0x8cc80020 # lw    r8,32(r6)         zero-filled BSS
.word 0x25080001 # addiu r8,r8,1
.word 0xacc80020 # sw    r8,32(r6)         BSS witness = 1
.word 0x3c094b4e # lui   r9,0x4b4e
.word 0x35294c43 # ori   r9,r9,0x4c43
.word 0xacc90010 # sw    r9,16(r6)         completion marker "CLNK"
.word 0x08040012 # j     0x00100048        park forever
.word 0x00000000 # nop                    delay slot
.section .data
.word 0x12345678
