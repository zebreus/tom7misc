
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

using namespace std;
using uint8 = uint8_t;
using uint16 = uint16_t;

static inline bool RowBit(uint16 row, int x) {
  return !!(row & (1 << (9 - x)));
}

static string RowString(uint16 row) {
  std::string ret;
  ret.resize(10);
  for (int x = 0; x < 10; x++) {
    ret[x] = RowBit(row, x) ? '#' : '.';
  }
  if (row & ~0b1111111111) return StringPrintf("Garbage-%02x-%s",
                                               row, ret.c_str());
  else return ret;
}

// Since there are only 2^8 possibilities for a line, we could do this
// by precomputing a plan for each pattern starting from some known
// position (e.g. vertical S in leftmost column), and with the
// constraint that we never depend on the cells below the target line
// (and on the maximum height). Then we just replay these in sequence.
// This might be the easiest way to do it, and it would be clear that
// the overall strategy is complete, even if we have to use heuristics
// to generate each pattern. Yeah, let's do this... it's a nice way
// to break the problem down and if it fails it will be instructive.

constexpr int ShapeWidth(Shape s) {
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
constexpr uint16 ShapeBits(Shape s) {
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

// Returns 4 rows (same representation as Tetris::rows) with a mask for
// the shape in the given column. x must be a valid location for the shape.
static constexpr std::array<uint16, 4> ShapeMaskInCol(Shape s, int x) {
  std::array<uint16, 4> rows;

  uint16 bits = ShapeBits(s);
  for (int r = 0; r < 4; r++) {
    // extract this row from the shape
    int shift = 4 * (3 - r);
    uint16 shape_row = (bits >> shift) & 0b1111;
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

// In this implementation, you can place any piece in any orientation
// in a given column (measured from leftmost cell in block). Drops are
// always straight down with no rotation/sliding. Aside from the
// original position, it is considered illegal for a piece to "land"
// on the bottom of the board; it must always land on existing filled
// cell.
struct Tetris {
  static constexpr int MAX_DEPTH = 8;
  // Low 10 bits of each word are used to denote the contents of the
  // cells; upper 6 are zero.
  std::array<uint16, MAX_DEPTH> rows;

  // Returns false if the placement is illegal.
  // XXX need to implement line clearing!
  bool Place(Shape shape, int x) {
    Piece piece = DecodePiece(shape);
    // Don't allow streaks since we may not be able to
    // realize them in practice.
    if (last_piece == piece)
      return false;

    // Off playfield. x is always the left edge of the piece.
    if (x < 0 || x + ShapeWidth(shape) > 10)
      return false;

    const std::array<uint16, 4> mask = ShapeMaskInCol(shape, x);

    auto Place = [this, piece, &mask](int r) {
        // This is where it would be placed. Make sure we wouldn't
        // need to set any cells off the board.
        for (int ro = 0; ro < 4; ro++) {
          int board_row_idx = r - ro;
          uint16 shape_row = mask[3 - ro];
          if (board_row_idx < 0 && shape_row != 0)
            return false;
        }

        // Okay, now blit and succeed.
        for (int ro = 0; ro < 4; ro++) {
          int board_row_idx = r - ro;
          uint16 shape_row = mask[3 - ro];

          if (board_row_idx >= 0) {
            CHECK((rows[board_row_idx] & shape_row) == 0) <<
              StringPrintf("r %d, ro %d, bri %d, shape_row %d\n"
                           "shape row: %s\n"
                           "board row: %s\nin:\n",
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

        last_piece = piece;
        return true;
      };

    // r is the bottom row where the piece might land.
    for (int r = 0; r < MAX_DEPTH; r++) {
      // See if there would be an intersection if we were
      // on the next row. Note that we may reach below the
      // ground when doing this.
      for (int ro = 0; ro < 4; ro++) {
        int next_board_row_idx = r + 1 - ro;
        uint16 shape_row = mask[3 - ro];
        // PERF: could avoid the branch by just storing one
        // more blank row.
        uint16 next_board_row =
          next_board_row_idx < MAX_DEPTH ? rows[next_board_row_idx] : 0;
        if ((shape_row & next_board_row) != 0) {
          return Place(r);
        }
      }
      // No intersection, so fall one more row.
    }

    // We don't allow landing on the "ground".
    printf("No land on ground!\n");
    return false;
  }

  Tetris() {
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

  Piece last_piece = PIECE_S;

  string BoardString() const {
    string ret;
    for (int row = 0; row < MAX_DEPTH; row++) {
      StringAppendF(&ret, "|%s|\n", RowString(rows[row]).c_str());
    }
    return ret;
  }

};

// Represents our current progress encoding the bits.
struct PlayState {
  // Lines completely done, counting from the bottom.
  // These always have 01bbbbbbbb, where bbb.. are the
  // correct bits. We don't touch them.
  int lines_done = 0;

  // In the line we're working on, the size of the
  // prefix (on the left) that has the correct contents.
  // This counts the constant 01 prefix, so has a maximum
  // meaningful value of 10 (but then we move on to the
  // next line).
  int prefix_done = 0;

};

[[maybe_unused]]
static void Encode(const std::vector<uint8> &bytes) {
  // XXX bogus copy

  // 0-based column.
  uint8 oriented_l = 0x0D;
  int offset_l = 0;
  // 1-based
  uint8 oriented_7 = 0x0F;
  int offset_7 = 1;
  // 1-based
  uint8 oriented_j = 0x04;
  int offset_j = 1;
  // 0-based
  uint8 oriented_f = 0x06;
  int offset_f = 0;

  uint8 oriented_o = 0x0A;
  int offset_o = 1;

  std::vector<std::tuple<Piece, uint8, int>> schedule = {
    // piece type, desired orientation, column
    make_tuple(PIECE_O, oriented_o, 0 + offset_o),
    make_tuple(PIECE_L, oriented_l, 2 + offset_l),
    make_tuple(PIECE_O, oriented_o, 0 + offset_o),
    make_tuple(PIECE_J, oriented_j, 4 + offset_j),
    make_tuple(PIECE_L, oriented_7, 2 + offset_7),
    make_tuple(PIECE_J, oriented_f, 4 + offset_f),
    make_tuple(PIECE_L, oriented_l, 6 + offset_l),
    make_tuple(PIECE_J, oriented_j, 8 + offset_j),
    make_tuple(PIECE_L, oriented_7, 6 + offset_7),
    make_tuple(PIECE_J, oriented_f, 8 + offset_f),
  };

  for (const auto &[piece, orientation, offset_] : schedule) {
    CHECK(piece == DecodePiece(orientation));
  }

  ArcFour rc("tetris");

  std::unique_ptr<Emulator> emu;
  emu.reset(Emulator::Create("tetris.nes"));
  CHECK(emu.get() != nullptr);

  vector<uint8> startmovie =
    SimpleFM7::ParseString(
        "!" // 1 player
        "554_7t" // press start on main menu
        "142_"
        "7c" // signals a place where we can wait to affect RNG (maybe)
        "45_5t" // press start on game type menu
        "82_7c" // another place to potentially wait (likely?)
        // "81_" // computed wait
        "13_" // computed wait for L|O, to match schedule below
        "8_7t" // press start on A-type menu to begin game
        "68_" // game started by now
                           );

  for (uint8 c : startmovie) emu->Step(c, 0);
  SaveScreenshot("start.png", emu.get());

  vector<uint8> outmovie = startmovie;

  vector<uint8> savestate;
  int savelen = 0;
  auto Save = [&emu, &outmovie, &savestate, &savelen]() {
      savelen = outmovie.size();
      savestate = emu->SaveUncompressed();
    };

  auto Restore = [&emu, &savestate, &savelen, &outmovie]() {
      outmovie.resize(savelen);
      emu->LoadUncompressed(savestate);
    };

  Save();

  // Now, repeatedly...

  // This starts at 2 because the current and next count as drops.
  uint8 prev_counter = 0x02;
  int retry_count = 0;
  int schedule_idx = 0;
  int64 frame = 0;
  int pieces = 0;
  for (;;) {
    // (note this is not really a frame counter currently, as it
    // does not get saved/restored)
    frame++;

    uint8 cur = emu->ReadRAM(MEM_CURRENT_PIECE);
    uint8 next_byte = emu->ReadRAM(MEM_NEXT_PIECE);
    Piece next = DecodePiece(next_byte);
    uint8 current_counter = emu->ReadRAM(MEM_DROP_COUNT);

    #if 0
    printf("%d frames sched %d. prev %d cur %d. cur %02x next %02x\n",
           (int)outmovie.size(), schedule_idx,
           prev_counter, current_counter, cur, next_byte);
    #endif

    if (cur == 0x13) {
      // Wait for lines to finish. Not a valid piece so we don't
      // want to enter the code below.
      // TODO: tetrises?
      emu->Step(0, 0);
      outmovie.push_back(0);
      continue;
    }

    if (prev_counter != current_counter) {
      // A piece just landed.
      // This means that 'cur' should just now have the next piece that
      // we previously established as correct.
      CHECK(DecodePiece(cur) == std::get<0>(schedule[(schedule_idx + 1) %
                                                     schedule.size()]));

      // Now check that the NEW next piece is what we expect.
      const Piece expected =
        std::get<0>(schedule[(schedule_idx + 2) % schedule.size()]);
      if (next != expected) {
        // We didn't get the expected piece.
        // Go back to previous piece. Also keep track of how many
        // times this has recently happened.
        retry_count++;
        if (retry_count > 64) {
          static bool first = true;
          if (first) SaveScreenshot("stuck.png", emu.get());
          first = false;

          const uint8 rng1 = emu->ReadRAM(MEM_RNG1);
          const uint8 rng2 = emu->ReadRAM(MEM_RNG2);
          printf("%d tries on piece %d (sched %d). cur: %02x=%c got next=%c want %c RNG %02x%02x fs %d\n",
                 retry_count, current_counter, schedule_idx,
                 cur, PieceChar(DecodePiece(cur)),
                 PieceChar(DecodePiece(next_byte)),
                 PieceChar(expected),
                 rng1, rng2,
                 (int)outmovie.size());
        }
        Restore();
        continue;
      }

      // Otherwise, new checkpoint.
      Save();
      retry_count = 0;
      schedule_idx++;
      schedule_idx %= schedule.size();
      pieces++;

      prev_counter = current_counter;
      if (pieces % 25 == 0) {
        SaveScreenshot(StringPrintf("lucky%d.png", pieces),
                       emu.get());

        SimpleFM2::WriteInputs("lucky.fm2",
                               "tetris.nes",
                               "base64:Ww5XFVjIx5aTe5avRpVhxg==",
                               outmovie);
        printf("Saved after dropping %d pieces.\n", pieces);
      }
    }

    uint8 cur_x = emu->ReadRAM(MEM_CURRENT_X);
    uint8 cur_orientation = emu->ReadRAM(MEM_CURRENT_PIECE);
    uint8 input = 0;

    const auto [piece_, orientation, column] = schedule[schedule_idx];
    CHECK(DecodePiece(cur) == piece_) <<
      StringPrintf("Expect %c but have %c=%02x?",
                   PieceChar(piece_),
                   PieceChar(DecodePiece(cur)), cur);

    if ((frame % 2) == 0) {
      // PERF: Can rotate in the most efficient direction.
      if (orientation != cur_orientation)
        input |= INPUT_A;

      if (column < cur_x) input |= INPUT_L;
      else if (column > cur_x) input |= INPUT_R;
    }

    if (orientation == cur_orientation &&
        column == cur_x) {
      // printf("OK %d %d\n", orientation, column);
      input = rc.Byte() & INPUT_D;
      // If we're having trouble, even try pausing
      if (retry_count > 64) input |= rc.Byte() & INPUT_T;
    }

    /*
    printf("Put %c in orientation %02x (now %02x) in column %d (now %d) "
           "input %s\n",
           PieceChar(piece_), orientation, cur_orientation,
           column, cur_x, SimpleFM2::InputToString(input).c_str());
    */

    emu->Step(input, 0);
    outmovie.push_back(input);
  }
}


int main(int argc, char **argv) {
  // Encode(std::vector<uint8>{0, 56, 68, 0, 44, 36, 0, 129});

  /*
  std::array<uint16, 4> a = ShapeMaskInCol(SQUARE, 8);
  for (uint16 u : a) {
    printf("Row: |%s|\n", RowString(u).c_str());
  }
  */

  /*
  Tetris tetris;
  printf("%s\n", tetris.BoardString().c_str());
  CHECK(tetris.Place(L_DOWN, 1));
  CHECK(tetris.Place(Z_HORIZ, 2));
  printf("%s\n", tetris.BoardString().c_str());
  */

  {
    Tetris tetris;
    printf("%s\n", tetris.BoardString().c_str());
    CHECK(tetris.Place(L_DOWN, 1));
    CHECK(tetris.Place(Z_HORIZ, 1));
    CHECK(tetris.Place(S_HORIZ, 2));
    CHECK(tetris.Place(I_VERT, 0));
    /*
      |..........|
      |#..##.....|
      |#.##......|
      |###.......|
      |#.##......|
      |###.......|
      |###.......|
      |.##.......|
    */
    CHECK(!tetris.Place(I_VERT, 0));
    CHECK(!tetris.Place(T_LEFT, 0));
    CHECK(tetris.Place(SQUARE, 1));
    CHECK(tetris.Place(I_HORIZ, 4));
    printf("%s\n", tetris.BoardString().c_str());
  }

  return 0;
}
