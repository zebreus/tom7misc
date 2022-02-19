
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

// We generally need to make many attempts to get the desired
// next piece, because it is RNG-dependent. The first time
// through, this  XXX docs
struct RetryState {

  RetryState(Emulator *emu, vector<uint8> *movie) : emu(emu), movie(movie) {
    Save();
  }

  // Save the state, once we have gotten to a new good point.
  void Save() {

    if (min_frames > 0) {
      printf("Success on %d (min_frames %d)\n", save_frame, min_frames);
      for (int i = 0; i < (int)delays_reached.size(); i++) {
        const auto &[count, rng] = delays_reached[i];
        if (count > 0) {
          printf("% 5d x% 5d  %s%s\n", i, count, RNGString(rng).c_str(),
                 i == (save_frame - min_frames) ? " <---" : "");
        }
      }
    } else {
      printf("Initial save or success on first try!\n");
    }
    printf("-----\n");
    
    save_frame = movie->size();
    savestate = emu->SaveUncompressed();
    rng_state = GetRNG(*emu);
    retry_count = 0;
    min_frames = -1;
    delays_reached.clear();
  }

  void Restore() {
    movie->resize(save_frame);
    emu->LoadUncompressed(savestate);
    rng_state = GetRNG(*emu);
    retry_count++;
  }

  // How many times have we tried dropping? If this is 0, then we
  // don't even yet know how many frames it takes, minimally.
  int retry_count = 0;

  void UpdateFrameTable(uint8 desired) {
    if (retry_count == 0) {
      min_frames = movie->size() - save_frame;
      delays_reached.clear();
      RNGState s = rng_state;
      for (int i = 0; i < HORIZON; i++) {
        delays_reached.emplace_back(0, s);
        s = NextRNG(s);
      }
      delays_reached[0].first++;
      CHECK(EqualRNG(delays_reached[0].second, rng_state));
    }

    // XXX
    const int offset = (movie->size() - save_frame) - min_frames;
    printf("Retry #%d, offset %d, rng %s\n", retry_count, offset,
           RNGString(rng_state).c_str());
    for (int i = 0; i < (int)delays_reached.size(); i++) {
      const auto &[count, rng] = delays_reached[i];
      if (count > 0) {
        printf("% 5d x% 5d  %s%s\n", i, count, RNGString(rng).c_str(),
               i == (save_frame - min_frames) ? " <---" : "");
      }
    }
    
    CHECK(offset >= 0);
    CHECK(offset < HORIZON) << "this is possible but it should never "
      "get this bad??";
    CHECK(offset < (int)delays_reached.size());
    delays_reached[offset].first++;
    CHECK(EqualRNG(delays_reached[offset].second, rng_state)) <<
      "On retry " << retry_count << " at offset " << offset << " expected "
                  << RNGString(delays_reached[offset].second) << " but got "
                  << RNGString(rng_state);
  }

  // Make a step in the emulator, and save it to the movie.
  void MakeStep(uint8 input) {

    rng_state = NextRNG(rng_state);
    
    CHECK(EqualRNG(rng_state, GetRNG(*emu))) << "This should only "
      "happen if a piece is dropped. On that frame, you must Save "
      "or Restore.";

    emu->Step(input, 0);
    steps_executed++;
    
    movie->push_back(input);
  }
  
  // This is only meaningfully set once retry_count > 0.
  // This is the number of frames after the Save() point that
  // it takes to drop the piece using our fastest approach
  // (put it into position and then hold 'down').
  int min_frames = 0;
  // starting at min_frames
  static constexpr int HORIZON = 10000;
  std::vector<std::pair<int, RNGState>> delays_reached;

  int StepsExecuted() const { return steps_executed; }
  
private:
  // The point we return to, in order to retry.
  vector<uint8> savestate;
  // And the length of the movie at that point.
  int save_frame = 0;

  int steps_executed = 0;

  // This state is tracked internally in each step, but is
  // only correct if the step does not spawn a piece.
  RNGState rng_state;
  
  // Not owned.
  Emulator *emu = nullptr;
  vector<uint8> *movie = nullptr;
  vector<RNGstate> rng_before;
};

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


  for (uint8 c : startmovie) {
    emu->Step(c, 0);
    steps_executed++;
  }
  if (callbacks.game_start)
    callbacks.game_start(*emu, (int)schedule.size());

  vector<uint8> outmovie = startmovie;  
  RetryState retry_state(emu.get(), &outmovie);
  
  // Keep track of the RNG state. We can just read it from RAM
  // whenever, but it is useful to quickly predict future values so we
  // can tell when we will get the piece we want. Every emulator frame
  // advances this (NextRNG). Additionally, spawning a piece
  // (NextPiece) can advance one more time if a re-roll happens.
  // RNGState tracked_state = GetRNG(*emu);
  // uint8 tracked_next_piece = NextPiece(tracked_state).last_drop;

  // Now, repeatedly...

  // This starts at 2 because the current and next count as drops.
  uint8 prev_counter = 0x02;
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
             retry_state.retry_count, rng1, rng2, last_drop, drop_count,
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

      /*
      CHECK(EqualRNG(tracked_state, GetRNG(*emu))) << 
        StringPrintf("tracked %s -> %02x actual %s\n",
                     RNGString(tracked_state).c_str(),
                     tracked_next_piece,
                     RNGString(GetRNG(*emu)).c_str());
      */

      retry_state->MakeStep(input);
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

      #if 0
      CHECK(next_byte == tracked_next_piece) <<
        [&]() {
          const RNGState actual_state = GetRNG(*emu);
          return
            StringPrintf("At frame %lld, pieces %d, retry_count %d:\n"
                         "Next is %02x, but tnp is %02x.\n"
                         "tracked RNG state %02x%02x.%02x.%02x\n"
                         "next piece state  %02x%02x.%02x.%02x\n"
                         "actual RNG state  %02x%02x.%02x.%02x\n",
                         frame, pieces, retry_state.retry_count,
                         next_byte, tracked_next_piece,
                         tracked_state.rng1, tracked_state.rng2,
                         tracked_state.last_drop, tracked_state.drop_count,
                         actual_state.rng1, actual_state.rng2,
                         actual_state.last_drop, actual_state.drop_count);
        }();
      #endif
      /*
        tracked_state = GetRNG(*emu);
        tracked_next_piece = NextPiece(tracked_state).last_drop;
      */
      
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

        // No, not GetRNG because we want the state BEFORE the call to
        // nextpiece, right?
        retry_state.UpdateFrameTable(expected);

        retry_state.Restore();
        if (callbacks.retried) {
          callbacks.retried(*emu, retry_state.retry_count, expected);
        }

        continue;
      }

      // Otherwise, new checkpoint.
      retry_state.Save();
      schedule_idx++;
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

    // Do we have to move into position?
    if (move.shape != cur_shape ||
        target_nes_x != cur_x) {

      // Always unpause if we aren't in the correct spot.
      if (is_paused && (frame % 3 == 0))
        input |= INPUT_T;
      
      if ((frame % 2) == 0) {
        // PERF: Can rotate in the most efficient direction.
        if (move.shape != cur_shape)
          input |= INPUT_A;

        if (target_nes_x < cur_x) input |= INPUT_L;
        else if (target_nes_x > cur_x) input |= INPUT_R;
      }

    } else {
      // We're in the correct position. Now we need to spend the
      // right number of frames to get the RNG into the right state
      // so that the next spawned piece is what we want.

      if (retry_state.retry_count == 0) {
        // If this is our first attempt, drop the piece as fast
        // as we can. We do this to establish the minimum frame,
        // from which we can try a target frame.
        input |= INPUT_D;
      } else {
        // XXX!
        input = (rc.Byte() & INPUT_D);
        if (retry_state.retry_count > 64) input |= (rc.Byte() & INPUT_T);
      }
        
    }

    /*
    CHECK(EqualRNG(tracked_state, GetRNG(*emu))) << 
      StringPrintf("tracked %s -> %02x actual %s\n",
                   RNGString(tracked_state).c_str(),
                   tracked_next_piece,
                   RNGString(GetRNG(*emu)).c_str());
    */

    retry_state->MakeStep(input);
  }
}
