
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
#include "nes-tetris.h"

using namespace std;
using uint8 = uint8_t;
using uint16 = uint16_t;

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
  }

  return 0;
}
