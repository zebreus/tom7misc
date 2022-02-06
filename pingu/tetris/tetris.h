
#ifndef _PINGU_TETRIS_H
#define _PINGU_TETRIS_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cmath>
#include <unordered_set>
#include <tuple>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"

#include "tetris.h"

// unrotated piece identity
enum Piece {
  // square
  PIECE_O = 0,
  PIECE_L,
  PIECE_J,
  PIECE_S,
  PIECE_Z,
  PIECE_I,
  PIECE_T,

  NUM_PIECES,
};

// A piece in a specific orientation, using the same byte as
// the NES uses.
enum Shape : uint8 {
  I_VERT = 0x11,
  I_HORIZ = 0x12,
  SQUARE = 0x0a,
  // given as the way the pointy side faces
  T_UP = 0x00,
  T_DOWN = 0x02,
  T_LEFT = 0x03,
  T_RIGHT = 0x01,

  // given as the way the long edge points
  J_UP = 0x04,  // J
  J_LEFT = 0x07, // logical not
  J_DOWN = 0x06, // r
  J_RIGHT = 0x05,

  Z_HORIZ = 0x08,
  Z_VERT = 0x09,

  S_HORIZ = 0x0b,
  S_VERT = 0x0C,

  // as the way the long edge points
  L_UP = 0x0D, // L
  L_LEFT = 0x10, // toboggan
  L_DOWN = 0x0F, // 7
  L_RIGHT = 0x0E,
};

// Return the position that the NES considers the shape
// to be in (MEM_CURRENT_X) when it is against the left
// wall.
static inline int ShapeOffset(Shape s) {
  switch (s) {
  case I_VERT: return 0;
  case I_HORIZ: return 2;
  case J_UP: return 1;
  case J_DOWN: return 0;
  case J_LEFT: return 1;
  case J_RIGHT: return 1;
  case Z_VERT: return 0;
  case Z_HORIZ: return 1;
  case SQUARE: return 1;
  case T_UP: return 1;
  case T_DOWN: return 1;
  case T_LEFT: return 1;
  case T_RIGHT: return 0;
  case L_UP: return 0;
  case L_DOWN: return 1;
  case L_LEFT: return 1;
  case L_RIGHT: return 1;
  case S_VERT: return 0;
  case S_HORIZ: return 1;
  }
  return 0;
}

static inline Piece DecodePiece(Shape s) {
  switch (s) {
  case I_VERT:
  case I_HORIZ: return PIECE_I;
  case J_UP:
  case J_DOWN:
  case J_LEFT:
  case J_RIGHT: return PIECE_J;
  case Z_VERT:
  case Z_HORIZ: return PIECE_Z;
  case SQUARE: return PIECE_O;
  case T_UP:
  case T_DOWN:
  case T_LEFT:
  case T_RIGHT: return PIECE_T;
  case L_UP:
  case L_DOWN:
  case L_LEFT:
  case L_RIGHT: return PIECE_L;
  case S_VERT:
  case S_HORIZ: return PIECE_S;
  default:
    LOG(FATAL) << "Can't decode byte: " << (int)s;
    return PIECE_I;
  }
}

// printable char
static inline char PieceChar(Piece p) {
  switch (p) {
  case PIECE_O: return 'O';
  case PIECE_L: return 'L';
  case PIECE_J: return 'J';
  case PIECE_S: return 'S';
  case PIECE_Z: return 'Z';
  case PIECE_I: return 'I';
  case PIECE_T: return 'T';
  default: return '?';
  }
}

// In this implementation, you can place any piece in any orientation
// in a given column (measured from leftmost cell in block). Drops are
// always straight down with no rotation/sliding. Aside from the
// original position, it is considered illegal for a piece to "land"
// on the bottom of the board; it must always land on an existing
// filled cell.
static inline bool RowBit(uint16_t row, int x) {
  return !!(row & (1 << (9 - x)));
}

static inline string RowString(uint16_t row) {
  std::string ret;
  ret.resize(10);
  for (int x = 0; x < 10; x++) {
    ret[x] = RowBit(row, x) ? '#' : '.';
  }
  if (row & ~0b1111111111) return StringPrintf("Garbage-%02x-%s",
                                               row, ret.c_str());
  else return ret;
}

// char '#' is set; anything else clear.
static inline uint16_t StringRow(const std::string s) {
  uint16 row = 0;
  for (int x = 0; x < 10; x++) {
    row <<= 1;
    row |= (s[x] == '#') ? 0b1 : 0b0;
  }
  return row;
}

static constexpr inline int ShapeWidth(Shape s) {
  switch (s) {
  case I_VERT: return 1;
  case I_HORIZ: return 4;
  case SQUARE: return 2;
  case T_UP: return 3;
  case T_DOWN: return 3;
  case T_LEFT: return 2;
  case T_RIGHT: return 2;
  case J_UP: return 2;
  case J_LEFT: return 3;
  case J_DOWN: return 2;
  case J_RIGHT: return 3;
  case Z_HORIZ: return 3;
  case Z_VERT: return 2;
  case S_HORIZ: return 3;
  case S_VERT: return 2;
  case L_UP: return 2;
  case L_LEFT: return 3;
  case L_DOWN: return 2;
  case L_RIGHT: return 3;
    // ?
  default: return 100;
  }
}

// Return the piece in the bottom left corner, packed into
// 4x4 bits.
static constexpr inline uint16_t ShapeBits(Shape s) {
  switch (s) {
  case I_VERT: return
      (0b1000 << 12) |
      (0b1000 << 8) |
      (0b1000 << 4) |
      (0b1000 << 0);
  case I_HORIZ: return
      (0b0000 << 12) |
      (0b0000 << 8) |
      (0b0000 << 4) |
      (0b1111 << 0);
  case SQUARE: return
      (0b0000 << 12) |
      (0b0000 << 8) |
      (0b1100 << 4) |
      (0b1100 << 0);
  case T_UP:  return
      (0b0000 << 12) |
      (0b0000 << 8) |
      (0b0100 << 4) |
      (0b1110 << 0);
  case T_DOWN:  return
      (0b0000 << 12) |
      (0b0000 << 8) |
      (0b1110 << 4) |
      (0b0100 << 0);
  case T_LEFT:  return
      (0b0000 << 12) |
      (0b0100 << 8) |
      (0b1100 << 4) |
      (0b0100 << 0);
  case T_RIGHT:  return
      (0b0000 << 12) |
      (0b1000 << 8) |
      (0b1100 << 4) |
      (0b1000 << 0);
  case J_UP:  return
      (0b0000 << 12) |
      (0b0100 << 8) |
      (0b0100 << 4) |
      (0b1100 << 0);
  case J_LEFT:  return
      (0b0000 << 12) |
      (0b0000 << 8) |
      (0b1110 << 4) |
      (0b0010 << 0);
  case J_DOWN:  return
      (0b0000 << 12) |
      (0b1100 << 8) |
      (0b1000 << 4) |
      (0b1000 << 0);
  case J_RIGHT:  return
      (0b0000 << 12) |
      (0b0000 << 8) |
      (0b1000 << 4) |
      (0b1110 << 0);
  case Z_HORIZ:  return
      (0b0000 << 12) |
      (0b0000 << 8) |
      (0b1100 << 4) |
      (0b0110 << 0);
  case Z_VERT:  return
      (0b0000 << 12) |
      (0b0100 << 8) |
      (0b1100 << 4) |
      (0b1000 << 0);
  case S_HORIZ:  return
      (0b0000 << 12) |
      (0b0000 << 8) |
      (0b0110 << 4) |
      (0b1100 << 0);
  case S_VERT:  return
      (0b0000 << 12) |
      (0b1000 << 8) |
      (0b1100 << 4) |
      (0b0100 << 0);
  case L_UP:  return
      (0b0000 << 12) |
      (0b1000 << 8) |
      (0b1000 << 4) |
      (0b1100 << 0);
  case L_LEFT:  return
      (0b0000 << 12) |
      (0b0000 << 8) |
      (0b0010 << 4) |
      (0b1110 << 0);
  case L_DOWN:  return
      (0b0000 << 12) |
      (0b1100 << 8) |
      (0b0100 << 4) |
      (0b0100 << 0);
  case L_RIGHT:  return
      (0b0000 << 12) |
      (0b0000 << 8) |
      (0b1110 << 4) |
      (0b1000 << 0);
    // ?
  default:  return
      (0b0001 << 12) |
      (0b0010 << 8) |
      (0b0100 << 4) |
      (0b1000 << 0);
  }
}

// Returns 4 rows (same representation as Game::rows) with a mask for
// the shape in the given column. x must be a valid location for the shape.
static inline constexpr std::array<uint16_t, 4>
ShapeMaskInCol(Shape s, int x) {
  std::array<uint16_t, 4> rows;

  uint16_t bits = ShapeBits(s);
  for (int r = 0; r < 4; r++) {
    // extract this row from the shape
    int shift = 4 * (3 - r);
    uint16_t shape_row = (bits >> shift) & 0b1111;
    // and put it in the output row
    // PERF if we set things up right, we could avoid
    // this conditional. But left shift by a negative
    // amount is not a right shift!
    if (x <= 6) {
      rows[r] = shape_row << (10 - 4 - x);
    } else {
      rows[r] = shape_row >> (x - 6);
    }
  }
  return rows;
}

// Tetris board with the given height.
// We use shorter fixed heights during search to make things faster.
template<int MAX_DEPTH_ARG>
struct TetrisDepth {
  static constexpr int MAX_DEPTH = MAX_DEPTH_ARG;
  // Low 10 bits of each word are used to denote the contents of the
  // cells; upper 6 are zero.
  std::array<uint16_t, MAX_DEPTH> rows;
  // PERF: maintaining a count of how many starting rows are 0
  // would allow us to make several loops faster.

  static constexpr bool VERBOSE = false;

  // Returns false if the placement is illegal.
  bool Place(Shape shape, int x) {
    Piece piece = DecodePiece(shape);
    // Don't allow streaks since we may not be able to
    // realize them in practice.
    if (last_piece == piece) {
      if (VERBOSE) printf("repeat piece\n");
      return false;
    }

    // Off playfield. x is always the left edge of the piece.
    if (x < 0 || x + ShapeWidth(shape) > 10) {
      if (VERBOSE) printf("off playfield\n");
      return false;
    }

    const std::array<uint16_t, 4> mask = ShapeMaskInCol(shape, x);

    auto Emplace = [this, shape, x, piece, &mask](int r) {
        // This is where it would be placed. Make sure we wouldn't
        // need to set any cells off the board. r might even be
        // negative, in which case we will also fail this test.
        for (int ro = 0; ro < 4; ro++) {
          int board_row_idx = r - ro;
          uint16_t shape_row = mask[3 - ro];
          if (board_row_idx < 0 && shape_row != 0) {
            if (VERBOSE) printf("r=%d but Off top %d\n", r, board_row_idx);
            return false;
          }
        }

        // Okay, now blit and succeed.
        for (int ro = 0; ro < 4; ro++) {
          int board_row_idx = r - ro;
          uint16_t shape_row = mask[3 - ro];

          if (board_row_idx >= 0) {
            CHECK((rows[board_row_idx] & shape_row) == 0) <<
              StringPrintf(
                  "Shape 0x%02x, x %d, r %d, ro %d, bri %d, shape_row %d\n"
                  "shape row: %s\n"
                  "board row: %s\nin:\n",
                  shape, x,
                  r, ro, board_row_idx, shape_row,
                  RowString(shape_row).c_str(),
                  RowString(rows[board_row_idx]).c_str()) <<
              BoardString();
            rows[board_row_idx] |= shape_row;
          } else {
            CHECK(shape_row == 0) <<
              StringPrintf("r %d, ro %d, bri %d, shape_row %d\n"
                           "shape row: %s\n"
                           "in:\n",
                           r, ro, board_row_idx, shape_row,
                           RowString(shape_row).c_str()) <<
              BoardString();
          }
        }

        // Nothing below row r has changed.
        ClearLines(r);
        last_piece = piece;
        if (VERBOSE) printf("OK:\n%s\n", BoardString().c_str());
        return true;
      };

    // r is the bottom row where the piece might land.
    // We start at -1 because we don't want to think that
    // we successfully landed at row 0 if there would be
    // a collision there!
    for (int r = -1; r < MAX_DEPTH; r++) {
      // See if there would be an intersection if we were
      // on the next row. Note that we may reach below the
      // ground when doing this.
      for (int ro = 0; ro < 4; ro++) {
        const int next_board_row_idx = r + 1 - ro;
        const uint16_t shape_row = mask[3 - ro];
        // PERF: could avoid the branch by storing blank rows
        // before and after?
        const uint16_t next_board_row =
          (next_board_row_idx >= 0 && next_board_row_idx < MAX_DEPTH) ?
          rows[next_board_row_idx] :
          0;
        if ((shape_row & next_board_row) != 0) {
          if (VERBOSE) {
            printf("Collide at r=%d ro=%d nbr=%d\n"
                   "shape_row: %s\n"
                   "next brow: %s\n", r, ro, next_board_row_idx,
                   RowString(shape_row).c_str(),
                   RowString(next_board_row).c_str());
          }
          return Emplace(r);
        }
      }
      // No intersection, so fall one more row.
    }

    // We don't allow landing on the "ground".
    if (VERBOSE) printf("no land on ground\n");
    return false;
  }

  TetrisDepth() {
    rows.fill(0);
    // starting position:
    //
    // #
    // ##
    //  #
    // -----------
    // XXX Note one rub: only I is known to be possible as the
    // next piece if the first is S, at the very beginning of the
    // game. But we can almost certainly find an opening sequence
    // that will get us to this state.
    rows[MAX_DEPTH - 1] = 256;
    rows[MAX_DEPTH - 2] = 256 + 512;
    rows[MAX_DEPTH - 3] = 512;
  }

  void SetLastPiece(Piece p) { last_piece = p; }
  Piece GetLastPiece() const { return last_piece; }

  string BoardString() const {
    string ret;
    for (int row = 0; row < MAX_DEPTH; row++) {
      StringAppendF(&ret, "|%s|\n", RowString(rows[row]).c_str());
    }
    return ret;
  }

  // Note you will not be able to place any pieces unless you then
  // SetRow etc.
  void ClearBoard() {
    rows.fill(0);
  }

  // Doesn't check that the state is legal (impossible empty or full
  // rows).
  void SetRowString(const std::string rowstring, int r) {
    CHECK(r >= 0 && r < MAX_DEPTH);
    rows[r] = StringRow(rowstring);
  }

private:
  // Clear any full lines at row <= bottom.
  // PERF: This approach makes one pass per line cleared.
  void ClearLines(int bottom) {
    for (int r = bottom; r >= 0; /* in loop */) {
      if (rows[r] == 0b1111111111) {
        // Shift
        for (int u = r; u > 0; u--) {
          rows[u] = rows[u - 1];
        }
        rows[0] = 0;
      } else {
        r--;
      }
    }
  }

  Piece last_piece = PIECE_S;
};

#endif
