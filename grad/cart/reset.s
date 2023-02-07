; Startup code for cc65/ca65

    .import _main
    .export __STARTUP__:absolute=1
    .importzp _client_nmi

; Linker generated symbols
    .import __STACK_START__, __STACKSIZE__
    .include "zeropage.inc"
    .import initlib, copydata


.segment "ZEROPAGE"

; no variables yet

skip: .byte $00
nops: .byte $00
dops: .byte $00
tops: .byte $00

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
  ldx $0
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


