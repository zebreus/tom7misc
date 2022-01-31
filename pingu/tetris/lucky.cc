
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/simplefm7.h"
#include "../fceulib/x6502.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "image.h"

using namespace std;
using uint8 = uint8_t;

constexpr int MEM_BOARD_START = 0x400;
constexpr int MEM_CURRENT_PIECE = 0x62;
constexpr int MEM_NEXT_PIECE = 0xBF;
constexpr int MEM_CURRENT_X = 0x40;
// (mod 256)
constexpr int MEM_DROP_COUNT = 0x1A;

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

// printable char
static char PieceChar(Piece p) {
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

// Square types:
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

static Piece DecodePiece(uint8 p) {
  switch (p) {
  case 0x11:
  case 0x12: return PIECE_I;
  case 0x7:
  case 0x6:
  case 0x5:
  case 0x4: return PIECE_J;
  case 0x8:
  case 0x9: return PIECE_Z;
  case 0x0A: return PIECE_O;
  case 0x0:
  case 0x1:
  case 0x2:
  case 0x3: return PIECE_T;
  case 0x0D:
  case 0x0E:
  case 0x0F:
  case 0x10: return PIECE_L;
  case 0x0B:
  case 0x0C: return PIECE_S;
  default:
    LOG(FATAL) << "Can't decode byte: " << (int)p;
    return PIECE_I;
  }
}


// 10x20, row major.
vector<uint8> GetBoard(const Emulator &emu) {
  vector<uint8> ret(10 * 20);
  for (int i = 0; i < 10 * 20; i++) {
    ret[i] = emu.ReadRAM(MEM_BOARD_START + i);
  }
  return ret;
}

void SaveScreenshot(const string &filename, Emulator *emu) {
  std::vector<uint8> save = emu->SaveUncompressed();
  emu->StepFull(0, 0);

  ImageRGBA img(emu->GetImage(), 256, 256);
  img.Save(filename);
  printf("Wrote %s\n", filename.c_str());

  emu->LoadUncompressed(save);
}

[[maybe_unused]]
static void HistoMenu() {
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
                           );

  vector<uint8> entergame =
    SimpleFM7::ParseString(
        "!" // 1 player
        "8_7t" // press start on A-type menu to begin game
        "68_" // game started by now
                           );

  for (uint8 c : startmovie) emu->Step(c, 0);
  for (uint8 c : entergame) emu->Step(c, 0);
  // SaveScreenshot("start.png", emu.get());

  // const std::vector<uint8> menu = emu->SaveUncompressed();

  std::vector<int> histo(NUM_PIECES * NUM_PIECES, 0);
  std::vector<int> example(NUM_PIECES * NUM_PIECES, -1);
  for (int wait = 0; wait < 65536; wait++) {
    // for (int f = 0; f < wait; f++) emu->Step(0, 0);

    // (could do this incrementally instead of starting from
    // the beginning each time..)

    // save on menu
    const std::vector<uint8> menustill = emu->SaveUncompressed();

    // And see what pieces we get if we start.
    for (uint8 c : entergame) emu->Step(c, 0);
    Piece cur = DecodePiece(emu->ReadRAM(MEM_CURRENT_PIECE));
    Piece next = DecodePiece(emu->ReadRAM(MEM_NEXT_PIECE));

    histo[next * NUM_PIECES + cur]++;
    if (example[next * NUM_PIECES + cur] == -1)
      example[next * NUM_PIECES + cur] = wait;

    if (wait % 100 == 0) printf("%d/%d\n", wait, 65536);

    emu->LoadUncompressed(menustill);

    // wait one more frame.
    uint8 b = rc.Byte();
    // None of these inputs do anything on this menu
    emu->Step(b & (INPUT_L | INPUT_A | INPUT_U | INPUT_S), 0);
  }

  for (int next = 0; next < NUM_PIECES; next++) {
    for (int cur = 0; cur < NUM_PIECES; cur++) {
      const int idx = next * NUM_PIECES + cur;
      printf("%c|%c|%d times|ex %d\n",
             PieceChar((Piece)next), PieceChar((Piece)cur),
             histo[idx], example[idx]);
    }
  }
}

[[maybe_unused]]
static void PieceDrop() {

  ArcFour rc("tetris");

  std::unique_ptr<Emulator> emu;
  emu.reset(Emulator::Create("tetris.nes"));
  CHECK(emu.get() != nullptr);

  vector<uint8> begin =
    SimpleFM7::ParseString(
        "!"
        "547_6t9_5t10_7t34_"
        "5d5_5d3_5d7_5d5_5d5_4d6_4d8_4d18_4d14_4d18_3d19_4d24_"
        "6t4_" // wait here, paused
                           );
  vector<uint8> drop_piece =
    SimpleFM7::ParseString(
        "!"
        "6t" // unpause
        "134_" // wait for piece to land
                           );

  for (uint8 c : begin) emu->Step(c, 0);

  std::vector<int> histo(NUM_PIECES * NUM_PIECES, 0);
  std::vector<int> example(NUM_PIECES * NUM_PIECES, -1);
  for (int wait = 0; wait < 65536; wait++) {
    // for (int f = 0; f < wait; f++) emu->Step(0, 0);

    // (could do this incrementally instead of starting from
    // the beginning each time..)

    const std::vector<uint8> paused = emu->SaveUncompressed();

    // And see what pieces we get if we start.
    for (uint8 c : drop_piece) emu->Step(c, 0);
    Piece cur = DecodePiece(emu->ReadRAM(MEM_CURRENT_PIECE));
    Piece next = DecodePiece(emu->ReadRAM(MEM_NEXT_PIECE));

    histo[next * NUM_PIECES + cur]++;
    if (example[next * NUM_PIECES + cur] == -1)
      example[next * NUM_PIECES + cur] = wait;

    if (wait % 100 == 0) printf("%d/%d\n", wait, 65536);
    if (wait == 0) SaveScreenshot("wait0.png", emu.get());

    emu->LoadUncompressed(paused);

    // wait one more frame.
    // uint8 b = rc.Byte();
    emu->Step(0, 0);
  }

  for (int next = 0; next < NUM_PIECES; next++) {
    for (int cur = 0; cur < NUM_PIECES; cur++) {
      const int idx = next * NUM_PIECES + cur;
      printf("%c|%c|%d times|ex %d\n",
             PieceChar((Piece)next), PieceChar((Piece)cur),
             histo[idx], example[idx]);
    }
  }
}


int main(int argc, char **argv) {

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
        "81_" // computed wait
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

  int target_column = 0;
  uint8 prev_counter = 0x7F;
  int retry_count = 0;
  // just one of these.
  bool square_ok = true;
  for (;;) {
    uint8 cur = emu->ReadRAM(MEM_CURRENT_PIECE);
    uint8 next_byte = emu->ReadRAM(MEM_NEXT_PIECE);
    Piece next = DecodePiece(next_byte);
    uint8 current_counter = emu->ReadRAM(MEM_DROP_COUNT);

    #if 0
    printf("%d frames. prev %d cur %d. cur %02x next %02x\n",
           (int)outmovie.size(),
           prev_counter, current_counter, cur, next_byte);
    #endif

    if (prev_counter != current_counter) {
      // piece has landed.
      const bool next_ok = (square_ok && next == PIECE_O) ||
        next == PIECE_I;
      if (!next_ok) {
        // We didn't get I as expected.
        // Go back to previous piece. Maybe should increment
        // some failure count and eventually start pausing?
        retry_count++;
        if (retry_count > 64) {
          printf("%d retries on piece %d. got next=%02x :/\n",
                 retry_count, current_counter, next_byte);
        }
        Restore();
        continue;
      }

      // Otherwise, new checkpoint.
      Save();
      retry_count = 0;
      square_ok = false;

      prev_counter = current_counter;
      if (true || prev_counter % 8 == 0) {
        SaveScreenshot(StringPrintf("lucky%d.png", prev_counter),
                       emu.get());

        SimpleFM2::WriteInputs("lucky.fm2",
                               "tetris.nes",
                               "base64:Ww5XFVjIx5aTe5avRpVhxg==",
                               outmovie);
        printf("Saved after dropping %d.\n", prev_counter);
      }

      // This also works for the two squares we place at the
      // beginning, which end up being equivalent to two I.
      target_column++;
      target_column %= 10;
    }

    uint8 cur_x = emu->ReadRAM(MEM_CURRENT_X);
    uint8 input = 0;
    switch (cur) {
    case 0x0A:
      // We start with two squares, which go in the
      // leftmost column. But this is column "1" for
      // squares.
      if (cur_x > 1) input = INPUT_L;
      break;
    case 0x12:
      // Bar but in wrong orientation.
      input = INPUT_B;
      break;
    case 0x11:
      if (cur_x > target_column) input = INPUT_L;
      else if (cur_x < target_column) input = INPUT_R;
      break;
    default:
      SaveScreenshot("error.png", emu.get());
      SimpleFM2::WriteInputs("error.fm2",
                             "tetris.nes",
                             "base64:Ww5XFVjIx5aTe5avRpVhxg==",
                             outmovie);
      LOG(FATAL) << StringPrintf(
          "counter %d retries %d target %d cur %02x next %02x\n",
          current_counter, retry_count, target_column,
          cur, next_byte);
    }

    if (input == 0) {
      input = rc.Byte() & INPUT_D;
      if (retry_count > 64) input |= rc.Byte() & INPUT_T;
    }

    emu->Step(input, 0);
    outmovie.push_back(input);
  }

  return 0;
}
