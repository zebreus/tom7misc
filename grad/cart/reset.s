; Startup code for cc65/ca65

    .import _main
    .export __STARTUP__:absolute=1
    .importzp _client_nmi

; Linker generated symbols
    .import __STACK_START__, __STACKSIZE__
    .include "zeropage.inc"
    .import initlib, copydata


.segment "ZEROPAGE"

;;;  these memory locations (on the zero page, at the beginning
;;;  of RAM) store the outcomes of executing various instructions,
;;;  with the idea that if the instruction is implemented incorrectly
;;;  that the memory locations will have the wrong value (and the
;;;  video output will thus be wrong too). The tests are not that
;;;  comprehensive, though!

skip: .byte $00
nops: .byte $00
dops: .byte $00
tops: .byte $00
oflo: .byte $00
rrao: .byte $00
rraa: .byte $00
saxo: .byte $00
asro: .byte $00
sreo: .byte $00
srea: .byte $00
shyo: .byte $00
shxo: .byte $00
arro: .byte $00
xaso: .byte $00
xass: .byte $00
atxo: .byte $00
sbco: .byte $00
isbo: .byte $00
isba: .byte $00
sedf: .byte $00
rlao: .byte $00
rlaa: .byte $00
dcpo: .byte $00
dcpf: .byte $00
alro: .byte $00
anco: .byte $00
ancf: .byte $00
an2o: .byte $00
an2f: .byte $00
sloo: .byte $00
xaao: .byte $00
axsx: .byte $00
axsf: .byte $00
ax2x: .byte $00
ax2f: .byte $00
laxa: .byte $00
laxx: .byte $00
laso: .byte $00
lasa: .byte $00
lasx: .byte $00
lass: .byte $00
ahxo: .byte $00

fina: .byte $00
finx: .byte $00
finy: .byte $00
finf: .byte $00
fins: .byte $00

savestack:  .byte $00

.segment "HEADER"

    .byte $4e,$45,$53,$1a
    .byte 01
    .byte 01
    .byte 00
    .byte 00
    .res 8,0


.segment "STARTUP"

start:
        sei
        cld

        ;; turn off frame counter IRQ.
        ldx #$40
        stx $4017

        ldx #$ff
        txs
        inx
        stx $2000
        stx $2001
        stx $4010
:
        lda $2002
        bpl :-
        lda #$00
Blankram:                       ;puts zero in all CPU RAM
        sta $00, x
        sta $0100, x
        sta $0200, x
        sta $0300, x
        sta $0400, x
        sta $0500, x
        sta $0600, x
        sta $0700, x
        inx
        bne Blankram

;;; NOP
  ldx #$00
  .byte $1a
  inx
  .byte $3a
  inx
  .byte $5a
  inx
  .byte $7a
  inx
  .byte $da
  inx
  .byte $fa
  inx
  ;; we incremented x 6 times, so we expect
  ;; 6 in this slot
  stx nops

;;; DOP - double no-ops.
;;; Here we place a KIL instruction after each
;;;  one, to ensure it is skipped.
  .byte $04,$02
  inx
  .byte $14,$02
  inx
  .byte $34,$02
  inx
  .byte $44,$02
  inx
  .byte $54,$02
  inx
  .byte $64,$02
  inx
  .byte $74,$02
  inx
  .byte $80,$02
  inx
  .byte $82,$02
  inx
  .byte $89,$02
  inx
  .byte $c2,$02
  inx
  .byte $d4,$02
  inx
  .byte $e2,$02
  inx
  .byte $f4,$02
  inx

;; want 20 (decimal)
  stx dops

;;; TOPS - triple no-ops!

  .byte $0c,$02,$42
  inx
  .byte $1c,$02,$42
  inx
  .byte $3c,$02,$42
  inx
  .byte $5c,$02,$42
  inx
  .byte $7c,$02,$42
  inx
  .byte $dc,$02,$42
  inx
  .byte $fc,$02,$42
  inx

;;; want 27 (decimal)
  stx tops

;;;  test CLV instruction
  lda #$f0
  adc #$81
;;;  overflow flag is set; clear it
  clv
;;;  now expect the jump is not taken
  bvs noflow
  sta oflo

noflow:

;;; test RRA
;;; start with something non-trivial
  lda #$81
  sta rrao
;;; RRA: read, rotate right, add with carry, write back
  .byte $6f
  .word rrao
  sta rraa

;;; SAX takes an immediate address and stores
;;; A & X there.
  lda #$0f
  .byte $8f
  .word saxo

;;; ASR (or LSR) loads an immediate, ANDS with A, shifts right
  lda #$8f
  .byte $4b,$88
;; result should be $44
  sta asro

;;; test SRE
  lda #$41
  sta sreo
;;; reads, left shift, or with A, write shifted value back
  .byte $4f
  .word sreo
  sta srea

;;;  SHY aka SYA
;;;  takes address, adds x, writes some function of y, resulting addr, x
  ldx #shyo
  ldy #$f7
  .byte $9c,$00,$00

;;;  SHX aka SXA
;;;  same thing, x reg. since the function computed is basically
;;;  ((0 + y) - y) + 1, we want something odd in x so that $01 is
;;;  written.
  ldy #shxo
  ldx #$cd
  .byte $9e,$00,$00


;;;  ARR - mysterious!
;;;  takes an immediate value, ands with A, does some weird shifting
;;;  with flags
  lda #$2A
  .byte $6b,$f3
  sta arro

;;; XAS aka TAS
;;;  takes immediate address, adds y, stores something weird based
;;;  on the address, Y, and stack registers. Also messes up the
;;;  stack register. bonkers!
  tsx
  stx savestack

  ldy #xaso
  ldx #$2b
  .byte $9b,$00,$00
;;;  observe stack reg is modified
  tsx
  stx xass

  ldx savestack
  txs

;;; ATX
;;; bizarre instruction that basically sets a and x to the immediate
;;;  value
  lda #$98
  .byte $ab,$13
  stx atxo

;;; SBC - undocumented opcode (same as $e9)
  lda #$2a
  .byte $eb,$11
  sta sbco

;;;  ISC aka ISB
;;;  read-modify-write, here from zero page. decrements the
;;;  value and subtracts it from a.
  ldx #$ee
  stx isbo
  .byte $e7,isbo
  sta isba

;;;   SED sets the decimal flag, which is unused on NES
  sed
  ;; push flags to stack, pop to read 'em
  php
  pla
  sta sedf

;;; RLA - read-modify-write: rotate left and AND
  ldx #$2a
  stx rlao
  lda #$f3
  .byte $2f
  .word rlao
  sta rlaa

;;; DCP - read-modify-write: decrement and compare
  stx dcpo
  ;;  using zero page addressing
  .byte $c7,dcpo
  ;;  save flags to see what compare did
  php
  pla
  sta dcpf

;;;  ALR aka ASR
  ;;  AND with immediate and shift right
  lda #$fe
  .byte $4b,$57
  sta alro

;;;  ANC aka AAC
;;;  AND with immediate, but update carry flag (?)
  lda #$ef
  .byte $0b,$87
  sta anco
  php
  pla
  sta ancf

  ;;  different opcode, same instruction
  lda #$5f
  .byte $2b,$47
  sta an2o
  php
  pla
  sta an2f

;;;  SLO: read-modify-write, shift left and OR

  ldx #$2a
  stx sloo
  .byte $07,sloo

;;; XAA. Takes an immediate. a = (a | 0xEE) & x, then a &= imm.
  ldx #$3f
  lda #$f7
  .byte $8b,$6f
  sta xaao

;;;  AXS. Takes immediate, "very similar to cmp"
  lda #$f7
  .byte $cb,$f1
  stx axsx
  php
  pla
  sta axsf
;;; now again with carry
  .byte $cb,$00
  stx ax2x
  php
  pla
  sta ax2f

;;; LAX - loads a and x (here from zero page)
  .byte $a7,tops
  sta laxa
  sta laxx

;;; LAS aka LAR read-modify-write, from addr+y, ands with s (??) and
;;; copies that to A and X. writes the same thing that was read.

  tsx
  stx savestack
  ;;  make stack not trivial
  php

  ;; put something non-trivial there, then load its offset
  ;; (base addr will be 0000)
  ldx #$7b
  stx laso
  ldy #laso

  .byte $bb,$00,$00
  sta lasa
  stx lasx
;;;  observe stack reg is modified
  tsx
  stx lass

;; and put it back
  ldx savestack
  txs

;;;  AHX aka AXA
;;; stores a & x & weirdhibyte to addr+y
  ldy #ahxo
;;;  want two odd numbers because weirdhibyte is basically 1
  lda #$33
  ldx #$f7
  .byte $9f,$00,$00

;;; now dump state, for one more chance of catching any
;;;  discrepancies

  sta fina
  stx finx
  sty finy
  php
  pla
  sta finf
  tsx
  stx fins

;;;  with the final expected memory contents (beginning of
;;;  zero page):
;;;  00 06 14 1b 71 c0 42 0b 44 20 61 01 01 91 01 01
;;;  13 18 ef 29 ec 54 50 29 3d 2b 87 bd 47 3c 54 2f
;;;  46 3C 04 3D 1b 1b 7b 7a 7a 7a 01 33 f7 2a bd ff
;;;  ff


  jmp ActualStart

  .asciiz "Test ROM #1 by Tom 7, 2023. Distribute freely. Exercises some undocumented and rare instructions. The bytes displayed in the video are the output (the beginning of the zero page). They are not checked by the ROM itself; instead you need to verify them against a known good emulator."

ActualStart:
:
        lda $2002
        bpl :-

Isprites:
        jsr Blanksprite
        lda #$00                ;pushes all sprites from 200-2ff
        sta $2003               ;to the sprite memory
        lda #$02
        sta $4014

        jsr ClearNT             ;puts zero in all PPU RAM

MusicInit:                      ;turns music channels off
        lda #0
        sta $4015

        lda #<(__STACK_START__+__STACKSIZE__)
        sta sp
        lda #>(__STACK_START__+__STACKSIZE__)
        sta sp+1                ; Set the c stack pointer

        jsr copydata
        jsr initlib

        lda $2002               ;reset the 'latch'
        jmp _main               ;jumps to main in c code


_Blanksprite:
Blanksprite:
        ldy #$40
        ldx #$00
        lda #$f8
Blanksprite2:           ;puts all sprites off screen
        sta $0200, x
        inx
        inx
        inx
        inx
        dey
        bne Blanksprite2
        rts


_ClearNT:
ClearNT:
        lda $2002
        lda #$20
        sta $2006
        lda #$00
        sta $2006
        lda #$00        ;tile 00 is blank
        ldy #$10
        ldx #$00
BlankName:              ;blanks screen
        sta $2007
        dex
        bne BlankName
        dey
        bne BlankName
        rts

nmi:
  inc _client_nmi
  ;;  fallthrough
irq:
    rti

.segment "RODATA"

;none yet

.segment "VECTORS"

    .word nmi   ;$fffa vblank nmi
    .word start ;$fffc reset
    .word irq   ;$fffe irq / brk


.segment "CHARS"

                ; .incbin "Alpha.chr"
    .incbin "tiles.chr"


