
#include "movie-maker.h"

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
#include "timer.h"

#include "tetris.h"
#include "nes-tetris.h"
#include "encoding.h"

MovieMaker::MovieMaker(const std::string &solution_file,
                       const std::string &rom_file,
                       int64_t seed) : rc(StringPrintf("%lld", seed)) {
  all_sols = Encoding::ParseSolutions(solution_file);
  CHECK(!all_sols.empty()) << solution_file;
  emu.reset(Emulator::Create(rom_file));
  CHECK(emu.get() != nullptr) << rom_file;
}


std::vector<uint8_t> MovieMaker::Play(const std::vector<uint8> &bytes,
                                      Callbacks callbacks) {
  // We can't use just any schedule because not all starting
  // pairs are possible. This one will work for the starting
  // movie below, which begins with O, L. Clears three lines
  // and then drops S_VERT at 0 to enter standard position.
  std::vector<Move> schedule = {
    // piece type, desired orientation, column
    Move(SQUARE, 0),
    Move(L_LEFT, 3),
    Move(J_RIGHT, 6),
    Move(L_RIGHT, 2),
    Move(J_LEFT, 7),
    // (two lines are cleared)
    Move(S_VERT, 0),
  };

  CHECK(Encoding::IsOkToEnd(DecodePiece(schedule.back().shape))) <<
    "Start schedule needs to end with an allowed ending piece.";

  // Now we should be able to just concatenate solutions
  // for each byte.
  for (int i = bytes.size() - 1; i >= 0; i--) {
    uint8 target = bytes[i];
    auto it = all_sols.find(target);
    CHECK(it != all_sols.end()) << "No solution for byte " << i;
    for (Move m : it->second) {
      schedule.push_back(m);
    }
  }

  // Validate that the approach below can execute this schedule.
  CHECK(schedule.size() > 2);
  CHECK(DecodePiece(schedule[0].shape) == PIECE_O &&
        DecodePiece(schedule[1].shape) == PIECE_L) << "Must "
    "begin with O, L to match the starting FM7 below.";

  for (int i = 1; i < (int)schedule.size(); i++) {
    CHECK(DecodePiece(schedule[i].shape) !=
          DecodePiece(schedule[i - 1].shape)) << "Schedule would "
      "require repeats, which are not always possible. At: " << i;
  }

  // TODO: Optimize this!
  const vector<uint8> startmovie =
    SimpleFM7::ParseString(
        "!" // 1 player
        "554_7t" // press start on main menu
        "142_"
        "7c" // signals a place where we can wait to affect RNG (maybe)
        "45_5t" // press start on game type menu
        "82_7c" // another place to potentially wait (likely?)
        "13_" // computed wait for L|O, to match schedule
        "8_7t" // press start on A-type menu to begin game
        "68_" // game started by now
                           );


  for (uint8 c : startmovie) emu->Step(c, 0);
  if (callbacks.game_start)
    callbacks.game_start(*emu, (int)schedule.size());

  vector<uint8> outmovie = startmovie;

  vector<uint8> savestate;
  int savelen = 0;
  auto Save = [this, &outmovie, &savestate, &savelen]() {
      savelen = outmovie.size();
      savestate = emu->SaveUncompressed();
    };

  auto Restore = [this, &savestate, &savelen, &outmovie]() {
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
  Timer run_timer;
  static constexpr int REPORT_EVERY = 10;
  int next_report = REPORT_EVERY;
  for (;;) {
    // (note this is not really a frame counter currently, as it
    // does not get saved/restored)
    frame++;

    uint8 cur_byte = emu->ReadRAM(MEM_CURRENT_PIECE);
    Shape cur = (Shape)cur_byte;
    Shape next_byte = (Shape)emu->ReadRAM(MEM_NEXT_PIECE);
    Piece next = DecodePiece(next_byte);
    uint8 current_counter = emu->ReadRAM(MEM_DROP_COUNT);

    const bool is_paused = IsPaused(*emu);

    int seconds = run_timer.Seconds();
    if (seconds >= next_report) {
      const uint8 rng1 = emu->ReadRAM(MEM_RNG1);
      const uint8 rng2 = emu->ReadRAM(MEM_RNG2);
      const uint8 last_drop = emu->ReadRAM(MEM_LAST_DROP);
      const uint8 drop_count = emu->ReadRAM(MEM_DROP_COUNT);

      const uint8 cur_x = emu->ReadRAM(MEM_CURRENT_X);
      const Move move = schedule[schedule_idx];
      const uint8 target_nes_x =
        move.col + ShapeXOffset(move.shape);

      double dps = schedule_idx / run_timer.Seconds();
      printf("%d frames sched %d/%d cnt %d -> %d. cur %02x next %02x\n"
             "  %.2f drops/sec. move %02x to %02x. have %02x at %02x\n"
             "  %d retries, RNG %02x%02x.%02x.%02x paused: %c last %s\n",
             (int)outmovie.size(), schedule_idx, (int)schedule.size(),
             prev_counter, current_counter, cur_byte, next_byte,
             dps, move.shape, target_nes_x, cur_byte, cur_x,
             retry_count, rng1, rng2, last_drop, drop_count,
             is_paused ? 'Y' : 'n',
             SimpleFM2::InputToString(outmovie.back()).c_str());
      next_report = seconds + REPORT_EVERY;
    }

    if (cur_byte == 0x13) {
      // Wait for lines to finish. Not a valid piece so we don't
      // want to enter the code below.
      // TODO: tetrises?

      // Seems we can get into this state paused; so un-pause!
      uint8 input = IsPaused(*emu) ? INPUT_T : 0;
      emu->Step(input, 0);
      outmovie.push_back(input);
      continue;
    }

    if (prev_counter != current_counter) {
      // A piece just landed.
      // This means that 'cur' should just now have the next piece that
      // we previously established as correct.
      if (schedule_idx + 1 < (int)schedule.size()) {
        CHECK(DecodePiece(cur) ==
              DecodePiece(schedule[(schedule_idx + 1) %
                                   schedule.size()].shape));
      } else {

        // Call callbacks one last time.
        if (callbacks.placed_piece)
          callbacks.placed_piece(*emu, schedule_idx + 1, schedule.size());
        /*
        if (callbacks.finished_byte)
          callbacks.finished_byte(*emu, XXX);
        */
        
        return outmovie;
      }

      // Now check that the NEW next piece is what we expect.
      // At the end of the schedule, we don't care what the next
      // piece is because we won't use it. Be permissive so that
      // we don't need to retry, and so that we don't accidentally
      // ask for an impossible repeat!
      const Piece expected =
        (schedule_idx + 2 < (int)schedule.size()) ?
        DecodePiece(schedule[schedule_idx + 2].shape) :
        next;
      if (next != expected) {
        // We didn't get the expected piece.
        // Go back to previous piece. Also keep track of how many
        // times this has recently happened.

        retry_count++;        
        if (callbacks.retried) {
          callbacks.retried(*emu, retry_count, expected);
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

      if (callbacks.placed_piece)
        callbacks.placed_piece(*emu, schedule_idx, (int)schedule.size());
      
      prev_counter = current_counter;
    }

    uint8 cur_x = emu->ReadRAM(MEM_CURRENT_X);
    Shape cur_shape = (Shape)emu->ReadRAM(MEM_CURRENT_PIECE);
    uint8 input = 0;

    const Move move = schedule[schedule_idx];
    // The shape should only mismatch due to orientation.
    CHECK(DecodePiece(cur_shape) == DecodePiece(move.shape)) <<
      StringPrintf("Expect %c but have %c=%02x?",
                   PieceChar(DecodePiece(move.shape)),
                   PieceChar(DecodePiece(cur_shape)), cur_shape);

    const uint8 target_nes_x =
      move.col + ShapeXOffset(move.shape);

    if ((frame % 2) == 0) {
      // PERF: Can rotate in the most efficient direction.
      if (move.shape != cur_shape)
        input |= INPUT_A;

      if (target_nes_x < cur_x) input |= INPUT_L;
      else if (target_nes_x > cur_x) input |= INPUT_R;
    }

    if (move.shape == cur_shape &&
        target_nes_x == cur_x) {
      // printf("OK %d %d\n", orientation, column);
      input = (rc.Byte() & INPUT_D);
      // If we're having trouble, even try pausing
      if (retry_count > 64) input |= (rc.Byte() & INPUT_T);
    }

    // Always consider unpausing if we are paused, to avoid some
    // (mysterious?) stuck states. (We may be pausing on exactly the
    // frame where we make a successful drop, and then staying
    // paused forever?)
    if (is_paused && (frame % 3 == 0)) {
      input |= INPUT_T;
    }

    emu->Step(input, 0);
    outmovie.push_back(input);
  }
}
