| hello-coff.S — minimal CP/M-68K BDOS hello-world used as a real
| m68k-coff Phase 2 fixture for mkdri.  Assembled by m68k-coff-as
| and linked by m68k-coff-ld at -Ttext 0x1700 (the CP/M-68K default
| user TPA load address).
|
| m68k-coff-as uses GAS Motorola syntax with register prefix `%`
| required (so we write `%a7` rather than `sp`).

    .text
    .globl  start
start:
    move.l  #msg, -(%a7)         | push pointer to $-terminated string
    move.w  #9, -(%a7)           | BDOS function 9 = PRTSTR
    trap    #2                   | CP/M-68K BDOS trap
    addq.l  #6, %a7

    clr.w   -(%a7)               | BDOS function 0 = exit
    trap    #2

    .data
msg:
    .ascii  "Hello from mkdri/Phase 2 via m68k-coff!"
    .byte   '$'                  | BDOS PRTSTR terminator

    .bss
    .lcomm  scratch, 64
