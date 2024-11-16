#include "opcodes.h"

#include <cstdint>

const char *const Opcodes::opcode_name[256] = {
  // 00
  "BRK",
  "ORA (d,x)",
  "STP",
  "SLO (d,x)",
  "NOP d",
  "ORA d",
  "ASL d",
  "SLO d",
  "PHP",
  "ORA #i",
  "ASL",
  "ANC #i",
  "NOP a",
  "ORA a",
  "ASL a",
  "SLO a",
  "BPL *+d",
  "ORA (d),y",
  "STP",
  "SLO (d),y",
  "NOP d,x",
  "ORA d,x",
  "ASL d,x",
  "SLO d,x",
  "CLC",
  "ORA a,y",
  "NOP",
  "SLO a,y",
  "NOP a,x",
  "ORA a,x",
  "ASL a,x",
  "SLO a,x",

  // 20
  "JSR a",
  "AND (d,x)",
  "STP",
  "RLA (d,x)",
  "BIT d",
  "AND d",
  "ROL d",
  "RLA d",
  "PLP",
  "AND #i",
  "ROL",
  "ANC #i",
  "BIT a",
  "AND a",
  "ROL a",
  "RLA a",
  "BMI *+d",
  "AND (d),y",
  "STP",
  "RLA (d),y",
  "NOP d,x",
  "AND d,x",
  "ROL d,x",
  "RLA d,x",
  "SEC",
  "AND a,y",
  "NOP",
  "RLA a,y",
  "NOP a,x",
  "AND a,x",
  "ROL a,x",
  "RLA a,x",

  // 40
  "RTI",
  "EOR (d,x)",
  "STP",
  "SRE (d,x)",
  "NOP d",
  "EOR d",
  "LSR d",
  "SRE d",
  "PHA",
  "EOR #i",
  "LSR",
  "ALR #i",
  "JMP a",
  "EOR a",
  "LSR a",
  "SRE a",
  "BVC *+d",
  "EOR (d),y",
  "STP",
  "SRE (d),y",
  "NOP d,x",
  "EOR d,x",
  "LSR d,x",
  "SRE d,x",
  "CLI",
  "EOR a,y",
  "NOP",
  "SRE a,y",
  "NOP a,x",
  "EOR a,x",
  "LSR a,x",
  "SRE a,x",

  // 60
  "RTS",
  "ADC (d,x)",
  "STP",
  "RRA (d,x)",
  "NOP d",
  "ADC d",
  "ROR d",
  "RRA d",
  "PLA",
  "ADC #i",
  "ROR",
  "ARR #i",
  "JMP (a)",
  "ADC a",
  "ROR a",
  "RRA a",
  "BVS *+d",
  "ADC (d),y",
  "STP",
  "RRA (d),y",
  "NOP d,x",
  "ADC d,x",
  "ROR d,x",
  "RRA d,x",
  "SEI",
  "ADC a,y",
  "NOP",
  "RRA a,y",
  "NOP a,x",
  "ADC a,x",
  "ROR a,x",
  "RRA a,x",

  // 80
  "NOP #i",
  "STA (d,x)",
  "NOP #i",
  "SAX (d,x)",
  "STY d",
  "STA d",
  "STX d",
  "SAX d",
  "DEY",
  "NOP #i",
  "TXA",
  "XAA #i",
  "STY a",
  "STA a",
  "STX a",
  "SAX a",
  "BCC *+d",
  "STA (d),y",
  "STP",
  "AHX (d),y",
  "STY d,x",
  "STA d,x",
  "STX d,y",
  "SAX d,y",
  "TYA",
  "STA a,y",
  "TXS",
  "TAS a,y",
  "SHY a,x",
  "STA a,x",
  "SHX a,y",
  "AHX a,y",

  // A0
  "LDY #i",
  "LDA (d,x)",
  "LDX #i",
  "LAX (d,x)",
  "LDY d",
  "LDA d",
  "LDX d",
  "LAX d",
  "TAY",
  "LDA #i",
  "TAX",
  "LAX #i",
  "LDY a",
  "LDA a",
  "LDX a",
  "LAX a",
  "BCS *+d",
  "LDA (d),y",
  "STP",
  "LAX (d),y",
  "LDY d,x",
  "LDA d,x",
  "LDX d,y",
  "LAX d,y",
  "CLV",
  "LDA a,y",
  "TSX",
  "LAS a,y",
  "LDY a,x",
  "LDA a,x",
  "LDX a,y",
  "LAX a,y",

  // C0
  "CPY #i",
  "CMP (d,x)",
  "NOP #i",
  "DCP (d,x)",
  "CPY d",
  "CMP d",
  "DEC d",
  "DCP d",
  "INY",
  "CMP #i",
  "DEX",
  "AXS #i",
  "CPY a",
  "CMP a",
  "DEC a",
  "DCP a",
  "BNE *+d",
  "CMP (d),y",
  "STP",
  "DCP (d),y",
  "NOP d,x",
  "CMP d,x",
  "DEC d,x",
  "DCP d,x",
  "CLD",
  "CMP a,y",
  "NOP",
  "DCP a,y",
  "NOP a,x",
  "CMP a,x",
  "DEC a,x",
  "DCP a,x",

  // E0
  "CPX #i",
  "SBC (d,x)",
  "NOP #i",
  "ISC (d,x)",
  "CPX d",
  "SBC d",
  "INC d",
  "ISC d",
  "INX",
  "SBC #i",
  "NOP",
  "SBC #i",
  "CPX a",
  "SBC a",
  "INC a",
  "ISC a",
  "BEQ *+d",
  "SBC (d),y",
  "STP",
  "ISC (d),y",
  "NOP d,x",
  "SBC d,x",
  "INC d,x",
  "ISC d,x",
  "SED",
  "SBC a,y",
  "NOP",
  "ISC a,y",
  "NOP a,x",
  "SBC a,x",
  "INC a,x",
  "ISC a,x",
};

uint8_t Opcodes::opcode_size[256] = {
  1 + 0, // 00, brk
  1 + 1, // 01, ora (zp,x)
  1 + 0, // 02, jam
  1 + 1, // 03, slo (zp,x)
  1 + 0, // 04, nop
  1 + 1, // 05, ora zp
  1 + 1, // 06, asl zp
  1 + 1, // 07, slo zp
  1 + 0, // 08, php
  1 + 1, // 09, ora #imm
  1 + 0, // 0a, asl
  1 + 1, // 0b, anc #imm
  1 + 2, // 0c, nop abs
  1 + 2, // 0d, ora abs
  1 + 2, // 0e, asl abs
  1 + 2, // 0f, slo abs

  1 + 1, // 10, bpl rel
  1 + 1, // 11, ora (zp),y
  1 + 0, // 12, jam
  1 + 1, // 13, slo (zp),y
  1 + 1, // 14, nop zp,x
  1 + 1, // 15, ora zp,x
  1 + 1, // 16, asl zp,x
  1 + 1, // 17, slo zp,x
  1 + 0, // 18, clc
  1 + 2, // 19, ora abs,y
  1 + 0, // 1a, nop
  1 + 2, // 1b, slo abs,y
  1 + 2, // 1c, nop abs,x
  1 + 2, // 1d, ora abs,x
  1 + 2, // 1e, asl abs,x
  1 + 2, // 1f, slo abs,x

  1 + 2, // 20, jsr abs
  1 + 1, // 21, and (zp,x)
  1 + 0, // 22, jam
  1 + 1, // 23, rla (zp,x)
  1 + 1, // 24, bit zp
  1 + 1, // 25, and zp
  1 + 1, // 26, rol zp
  1 + 1, // 27, rla zp
  1 + 0, // 28, plp
  1 + 1, // 29, and #imm
  1 + 0, // 2a, rol
  1 + 1, // 2b, anc #imm
  1 + 2, // 2c, bit abs
  1 + 2, // 2d, and abs
  1 + 2, // 2e, rol abs
  1 + 2, // 2f, rla abs

  1 + 1, // 30, bmi rel
  1 + 1, // 31, and (zp),y
  1 + 0, // 32, jam
  1 + 1, // 33, rla (zp),y
  1 + 1, // 34, nop zp,x
  1 + 1, // 35, and zp,x
  1 + 1, // 36, rol zp,x
  1 + 1, // 37, rla zp,x
  1 + 0, // 38, sec
  1 + 2, // 39, and abs,y
  1 + 0, // 3a, nop
  1 + 1, // 3b, rla abs,y
  1 + 2, // 3c, nop abs,x
  1 + 2, // 3d, and abs,x
  1 + 2, // 3e, rol abs,x
  1 + 2, // 3f, rla abs,x

  1 + 0, // 40, rti
  1 + 1, // 41, eor (zp,x)
  1 + 0, // 42, jam
  1 + 1, // 43, sre (zp,x)
  1 + 1, // 44, nop zp
  1 + 1, // 45, eor zp
  1 + 1, // 46, lsr zp
  1 + 1, // 47, sre zp
  1 + 0, // 48, pha
  1 + 1, // 49, eor #imm
  1 + 0, // 4a, lsr
  1 + 1, // 4b, asl #imm
  1 + 2, // 4c, jmp abs
  1 + 2, // 4d, eor abs
  1 + 2, // 4e, lsr abs
  1 + 2, // 4f, sre abs

  1 + 1, // 50, bvs rel
  1 + 1, // 51, eor (zp),y
  1 + 0, // 52, jam
  1 + 1, // 53, sre (zp),y
  1 + 1, // 54, nop zp,x
  1 + 1, // 55, eor zp,x
  1 + 1, // 56, lsr zp,x
  1 + 1, // 57, sre zp,x
  1 + 0, // 58, cli
  1 + 2, // 59, eor abs,y
  1 + 0, // 5a, nop
  1 + 2, // 5b, sre abs,y
  1 + 2, // 5c, nop abs,x
  1 + 2, // 5d, eor abs,x
  1 + 2, // 5e, lsr abs,x
  1 + 2, // 5f, sre abs,x

  1 + 0, // 60, rts
  1 + 1, // 61, adc (zp,x)
  1 + 0, // 62, jam
  1 + 1, // 63, rra (zp,x)
  1 + 1, // 64, nop zp
  1 + 1, // 65, adc zp
  1 + 1, // 66, ror zp
  1 + 1, // 67, rra zp
  1 + 0, // 68, pla
  1 + 1, // 69, adc #imm
  1 + 0, // 6a, ror
  1 + 1, // 6b, arr #imm
  1 + 2, // 6c, jmp (abs)
  1 + 2, // 6d, adc abs
  1 + 2, // 6e, ror abs
  1 + 2, // 6f, rra abs

  1 + 1, // 70, bvs rel
  1 + 1, // 71, adc (zp),y
  1 + 0, // 72, jam
  1 + 1, // 73, rra (zp),y
  1 + 1, // 74, nop zp,x
  1 + 1, // 75, adc zp,x
  1 + 1, // 76, ror zp,x
  1 + 1, // 77, rra zp,x
  1 + 0, // 78, sei
  1 + 2, // 79, adc abs,y
  1 + 0, // 7a, nop
  1 + 2, // 7b, rra abs,y
  1 + 2, // 7c, nop abs,x
  1 + 2, // 7d, adc abs,x
  1 + 2, // 7e, ror abs,x
  1 + 2, // 7f, rra abs,x

  1 + 1, // 80, nop #imm
  1 + 1, // 81, sta (zp,x)
  1 + 1, // 82, nop #imm
  1 + 1, // 83, sax (zp,x)
  1 + 1, // 84, sty zp
  1 + 1, // 85, sta zp
  1 + 1, // 86, stx zp
  1 + 1, // 87, sax zp
  1 + 0, // 88, dey
  1 + 1, // 89, nop #imm
  1 + 0, // 8a, txa
  1 + 1, // 8b, xaa #imm
  1 + 2, // 8c, sty abs
  1 + 2, // 8d, sta abs
  1 + 2, // 8e, stx abs
  1 + 2, // 8f, sax abs

  1 + 1, // 90, bcc rel
  1 + 1, // 91, sta (zp),y
  1 + 0, // 92, jam
  1 + 1, // 93, ahx (zp),y
  1 + 1, // 94, sty zp,x
  1 + 1, // 95, sta zp,x
  1 + 1, // 96, stx zp,y
  1 + 1, // 97, sax zp,y
  1 + 0, // 98, tya
  1 + 2, // 99, sta abs,y
  1 + 0, // 9a, txs
  1 + 2, // 9b, tas abs,y
  1 + 2, // 9c, shf abs,x
  1 + 2, // 9d, sta abs,x
  1 + 2, // 9e, shx abs,y
  1 + 2, // 9f, ahx abs,y

  1 + 1, // a0, ldy #imm
  1 + 1, // a1, lda (zp,x)
  1 + 1, // a2, ldx #imm
  1 + 1, // a3, lax (zp,x)
  1 + 1, // a4, ldy zp
  1 + 1, // a5, lda zp
  1 + 1, // a6, ldx zp
  1 + 1, // a7, lax zp
  1 + 0, // a8, tay
  1 + 1, // a9, lda #imm
  1 + 0, // aa, tax
  1 + 1, // ab, lax #imm
  1 + 2, // ac, ldy abs
  1 + 2, // ad, lda abs
  1 + 2, // ae, ldx abs
  1 + 2, // af, lax abs

  1 + 1, // b0, bcs rel
  1 + 1, // b1, lda (zp),y
  1 + 0, // b2, jam
  1 + 1, // b3, lax (zp),y
  1 + 1, // b4, ldy zp,x
  1 + 1, // b5, lda zp,x
  1 + 1, // b6, ldx zp,y
  1 + 1, // b7, lax zp,y
  1 + 0, // b8, clv
  1 + 2, // b9, lda abs,y
  1 + 0, // ba, tsx
  1 + 2, // bb, las abs,y
  1 + 2, // bc, ldy abs,x
  1 + 2, // bd, lda abs,x
  1 + 2, // be, ldx abs,y
  1 + 2, // bf, lax abs,y

  1 + 1, // c0, cpy #imm
  1 + 1, // c1, cmp (zp,x)
  1 + 1, // c2, nop #imm
  1 + 1, // c3, dcp (zp,x)
  1 + 1, // c4, cpy zp
  1 + 1, // c5, cmp zp
  1 + 1, // c6, dec zp
  1 + 1, // c7, dcp zp
  1 + 0, // c8, iny
  1 + 1, // c9, cmp #imm
  1 + 0, // ca, dex
  1 + 1, // cb, sbx #imm
  1 + 2, // cc, cpy abs
  1 + 2, // cd, cmp abs
  1 + 2, // ce, dec abs
  1 + 2, // cf, dcp abs

  1 + 1, // d0, bne rel
  1 + 1, // d1, cmp (zp),y
  1 + 0, // d2, jam
  1 + 1, // d3, dcp (zp),y
  1 + 1, // d4, nop zp,x
  1 + 1, // d5, cmp zp,x
  1 + 1, // d6, dec zp,x
  1 + 1, // d7, dcp zp,x
  1 + 0, // d8, cld
  1 + 2, // d9, cmp abs,y
  1 + 0, // da, nop
  1 + 2, // db, dcp abs,y
  1 + 2, // dc, nop abs,x
  1 + 2, // dd, cmp abs,x
  1 + 2, // de, dec abs,x
  1 + 2, // df, dcp abs,x

  1 + 1, // e0, cpx #imm
  1 + 1, // e1, sbc (zp,x)
  1 + 1, // e2, nop #imm
  1 + 1, // e3, isc (zp,x)
  1 + 1, // e4, cpx zp
  1 + 1, // e5, sbc zp
  1 + 1, // e6, inc zp
  1 + 1, // e7, isc zp
  1 + 0, // e8, inx
  1 + 1, // e9, sbc #imm
  1 + 0, // ea, nop
  1 + 1, // eb, sbc #imm
  1 + 2, // ec, cpx abs
  1 + 2, // ed, sbc abs
  1 + 2, // ee, inc abs
  1 + 2, // ef, isc abs

  1 + 1, // f0, beq rel
  1 + 1, // f1, sbc (zp),y
  1 + 0, // f2, jam
  1 + 1, // f3, isc (zp),y
  1 + 1, // f4, nop zp,x
  1 + 1, // f5, sbc zp,x
  1 + 1, // f6, inc zp,x
  1 + 1, // f7, isc zp,x
  1 + 0, // f8, sed
  1 + 2, // f9, sbc abs,y
  1 + 0, // fa, nop
  1 + 2, // fb, isc abs,y
  1 + 2, // fc, nop abs,x
  1 + 2, // fd, sbc abs,x
  1 + 2, // fe, inc abs,x
  1 + 2, // ff, isc abs,x
};
