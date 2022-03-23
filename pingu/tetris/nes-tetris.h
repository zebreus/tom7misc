
#ifndef _PINGU_NES_TETRIS_H
#define _PINGU_NES_TETRIS_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cmath>
#include <unordered_set>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/simplefm7.h"
#include "../fceulib/x6502.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "image.h"

#include "tetris.h"

constexpr int MEM_RNG1 = 0x17;
constexpr int MEM_RNG2 = 0x18;
// "current" piece in its standard orientation
constexpr int MEM_LAST_DROP = 0x19;
// (mod 256)
constexpr int MEM_DROP_COUNT = 0x1A;

constexpr int MEM_BOARD_START = 0x400;
constexpr int MEM_CURRENT_PIECE = 0x62;
constexpr int MEM_NEXT_PIECE = 0xBF;
constexpr int MEM_CURRENT_X = 0x40;
constexpr int MEM_CURRENT_Y = 0x41;

// XXX maybe this should go in tetris.h and this
// file should only be Emulator stuff?

// 0x17-0x1A
struct RNGState {
  // Initialized to constants at addr 0x80BC.
  uint8_t rng1 = 0x89; // MEM_RNG1
  uint8_t rng2 = 0x88; // MEM_RNG2

  // Cleared when zeroing out memory, then again.
  uint8_t last_drop = 0x00;  // MEM_LAST_DROP
  // Cleared when zeroing out the memory on boot.
  uint8_t drop_count = 0x00;  // MEM_DROP_COUNT
};


inline bool EqualRNG(RNGState a, RNGState b) {
  return a.rng1 == b.rng1 &&
    a.rng2 == b.rng2 &&
    a.last_drop == b.last_drop &&
    a.drop_count == b.drop_count;
}

inline std::string RNGString(RNGState s) {
  return StringPrintf("%02x%02x.%02x.%02x",
                      s.rng1, s.rng2,
                      s.last_drop, s.drop_count);
}

// This is the code at 0xAB47. Every JSR to this sets
// X = 17 (M_RNG1) and Y = 2 (its size), so we just
// take those as constants.
//
// This is a two-tap LFSR (see equivalent FastNextRNG
// below). Its period is 32767.
[[maybe_unused]]
inline RNGState NextRNG(RNGState s) {
  uint8_t x = 17;
  uint8_t y = 2;
  // this is zeropage[x], i.e. rng1
  //      lda     $00,x
  uint8_t a = s.rng1;
  // # means the immediate value 02
  //      and     #$02
  a &= 0x02;
  // (flags?)

  //      sta     $00
  uint8_t zp0 = a;

  // this is zeropage[x + 1], i.e. rng2
  //      lda     $01,x
  a = s.rng2;

  //      and     #$02
  a &= 0x02;

  //      eor     $00
  a ^= zp0;
  bool zero_flag = (a == 0x00);

  //      clc
  bool carry_flag = false;

  //      beq     $AB57
  if (zero_flag)
    goto _AB57;

  //      sec
  carry_flag = true;

  // Looks like this just boils down to a 16-bit
  // rotate, once we have the carry flag set.
 _AB57:
  //      ror     $00,x
  bool ctmp1 = (s.rng1 & 0x1) == 0x1;
  s.rng1 = (s.rng1 >> 1) | (carry_flag ? 128 : 0);
  carry_flag = ctmp1;

  // these do not affect carry flag.
  //      inx
  x++;
  //      dey
  y--;
  zero_flag = (y == 0x00);

  //      bne     $AB57
  // assuming y is always 2, runs one more time.
  CHECK(!zero_flag);
  bool ctmp2 = (s.rng2 & 0x1) == 0x1;
  s.rng2 = (s.rng2 >> 1) | (carry_flag ? 128 : 0);
  carry_flag = ctmp2;

  x++;
  y--;
  zero_flag = (y == 0x00);
  //      bne     $AB57
  CHECK(zero_flag);

  //      rts
  return s;
}

// Same, but implemented as regular C code.
[[maybe_unused]]
inline RNGState FastNextRNG(RNGState s) {
  // Pack
  uint16 state = (s.rng1 << 8) | s.rng2;

  uint16 carry = ((state >> 9) ^ (state >> 1)) & 1;
  state = (state >> 1) | (carry << 15);

  // Unpack.
  s.rng1 = (state >> 8) & 0xFF;
  s.rng2 = state & 0xFF;
  return s;
}


// Generate next piece.
// This is the code at $9907.
[[maybe_unused]]
inline RNGState NextPiece(RNGState s) {
  constexpr std::array<uint8_t, 8> PIECES = {
    0x02, 0x07, 0x08, 0x0A, 0x0B, 0x0E, 0x12,
    // not used ?
    0x02,
  };

  uint8_t x = 0;

  //  inc $001a  (MEM_DROP_COUNT)
  s.drop_count++;
  //  lda $0017  (MEM_RNG_1);
  uint8_t a = s.rng1;
  //  clc
  bool carry_flag = false;
  //  adc $001a = #$0a
  // (recall NES does not even implement decimal flag)
  a += s.drop_count;

  //  and #$07
  a &= 0x7;
  //  cmp #$07
  //  beq $991c
  if (a == 0x7)
    goto _991C;

  //  tax
  x = a;

  // 994e is a (constant?) table of pieces in their
  // original orientations
  //  lda $994e,x
  a = PIECES[x];

  // If this is a new piece, succeed.
  //  cmp $0019  (MEM_LAST_DROP)
  //  bne $9938
  if (a != s.last_drop)
    goto _9938;

 _991C:
  // We get here if the first random number was out
  // of bounds, or the drop was the same as the
  // previous.

  // printf("Re-roll! a=%02x  rng = %02x%02x ", a, s.rng1, s.rng2);
  
  //  ldx #$17
  //  ldy #$02
  //  jsr $ab47  (NextRNG)
  s = NextRNG(s);

  // printf("-> %02x%02x\n", s.rng1, s.rng2);
  
  //  lda $0017
  a = s.rng1;
  //  and #$07
  a &= 0x7;
  //  clc
  carry_flag = false;
  //  adc $0019
  // printf("a %02x + %02x -> %02x\n", a, s.last_drop, a + s.last_drop);
  a += s.last_drop;

 _992A:
  // This code is (a %= 7) implemented as a loop,
  // which maybe could be done faster since a and
  // last_drop both have known ranges.

  // printf("At 992a, a=%02x\n", a);
  
  //  cmp #$07
  carry_flag = (a >= 0x07);
  //  bcc $9934
  if (!carry_flag)
    goto _9934;

  //  sec
  carry_flag = true;
  //  sbc #$07
  a -= 7;

  goto _992A;

 _9934:
  //  tax
  x = a;
  //  lda $994e,x
  a = PIECES[x];
  // In this situation we fall through to allowing
  // the drop to be equal.

 _9938:
  // Success!
  //  sta $0019  (MEM_LAST_DROP)
  s.last_drop = a;

  //  rts
  return s;
}

// Simplified version of NextPiece.
[[maybe_unused]]
inline RNGState FastNextPiece(RNGState s) {
  constexpr std::array<uint8_t, 8> PIECES = {
    0x02, 0x07, 0x08, 0x0A, 0x0B, 0x0E, 0x12,
    // not used
    0x02,
  };

  s.drop_count++;

  uint8_t a = (s.rng1 + s.drop_count) & 7;

  if (a == 7 || PIECES[a] == s.last_drop) {
    // re-roll if out of bounds, or repeat
    s = NextRNG(s);
    // mod 7 forces in-bounds, but repeats allowed
    a = ((s.rng1 & 7) + s.last_drop) % 7;
  }
  
  s.last_drop = PIECES[a];
  return s;
}


// Square types in board array (0x400+):
// EF = empty
// 7B = white square w/shine, like in T, square, line
// 7D = blue, like in J
// 7C = cyan, like Z

// mems
// 1a: counts up with each block
// 0x40, 0x60: dropping block's x pos.
//   this depends on rotation somehow; you can't put it
//   in x=0 in some rotations.
// 0xb1: frame counter, goes when paused
// 0x17, 0x18: probably RNG? goes when paused

// next piece: 0x19, 0xBF (seem equal). BF can be modified
//   and overwrites 19.
//
// 0x0A - square
// 0x0E - L shape
// 0x0B - S
// 0x02 - T ?
// 0x07 - J
// 0x08 - Z
// 0x12 - bar

// Give the initial orientation that the NES always drops a piece in.
inline Shape DropShape(Piece p) {
  switch (p) {
  default:
  case PIECE_O: return SQUARE;
  case PIECE_L: return L_RIGHT;
  case PIECE_J: return J_LEFT;
  case PIECE_S: return S_HORIZ;
  case PIECE_Z: return Z_HORIZ;
  case PIECE_I: return I_HORIZ;
  case PIECE_T: return T_DOWN;
  }
}

// These all hold the current piece: 0x42, 0x62, 0xAC.
// overwriting 0x62 immediately changes the current piece, cool.
//
// Note that these have rotations:
// 0x11 - rotated bar (vertical)
// 0x7, 0x6, 0x5, 0x4 - rotated J
// 0x8, 0x9 - z
// 0x0a - square
// 0x0, 0x1, 0x2, 0x3 - T
// 0x0D, 0x0E, 0x0F, 0x10 - L
// 0xB, 0xC - S
// this all makes sense!

inline RNGState GetRNG(const Emulator &emu) {
  RNGState state;

  state.rng1 = emu.ReadRAM(MEM_RNG1);
  state.rng2 = emu.ReadRAM(MEM_RNG2);  
  state.last_drop = emu.ReadRAM(MEM_LAST_DROP);
  state.drop_count = emu.ReadRAM(MEM_DROP_COUNT);

  return state;
}

// 10x20, row major.
inline std::vector<uint8_t> GetBoard(const Emulator &emu) {
  std::vector<uint8_t> ret(10 * 20);
  for (int i = 0; i < 10 * 20; i++) {
    ret[i] = emu.ReadRAM(MEM_BOARD_START + i);
  }
  return ret;
}

// Draw the shape on on the board (20x10, NES format) using the given
// byte. The x,y coordinate is the BOTTOM left of the shape.
inline void DrawShapeOnBoard(uint8_t b, Shape shape, int x, int y,
							 std::vector<uint8_t> *board) {

  std::array<uint16_t, 4> mask = ShapeMaskInCol(shape, x);
  for (int my = 0; my < 4; my++) {
	int yy = y - 3 + my;
	if (yy < 0 || yy >= 20) continue;
	
	uint16_t m = mask[my];
	for (int x = 0; x < 10; x++) {
	  if (m & (1 << (9 - x))) {
		(*board)[yy * 10 + x] = b;
	  }
	}
  }
}


inline void SaveScreenshot(const string &filename, Emulator *emu) {
  std::vector<uint8_t> save = emu->SaveUncompressed();
  emu->StepFull(0, 0);

  ImageRGBA img(emu->GetImage(), 256, 256);
  img.Save(filename);
  printf("Wrote %s\n", filename.c_str());

  emu->LoadUncompressed(save);
}

// True if we're in the middle of clearing a line.
inline bool IsLineClearing(const Emulator &emu) {
  return emu.ReadRAM(MEM_CURRENT_PIECE) == 0x13;
}

// Heuristic; not sure if this is correct.
// (Probably program counter would be definitive when
// outside of NMI.)
inline bool IsPaused(const Emulator &emu) {
  return emu.ReadRAM(0x00a0) == 0x70 &&
    emu.ReadRAM(0x00a1) == 0x77 &&
    emu.ReadRAM(0x00a2) == 0x05;
}

#endif
