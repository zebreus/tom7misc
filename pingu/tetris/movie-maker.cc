
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

static constexpr bool VERBOSE = false;

static constexpr int64 CALLBACK_EVERY_STEPS = 500;

MovieMaker::MovieMaker(const std::string &solution_file,
                       const std::string &rom_file,
                       int64_t seed) :
  MovieMaker(solution_file,
			 std::unique_ptr<Emulator>(Emulator::Create(rom_file)),
			 seed) {}

MovieMaker::MovieMaker(const std::string &solution_file,
                       std::unique_ptr<Emulator> emulator,
                       int64_t seed) :
  rc(StringPrintf("%lld", seed)),
  emu(std::move(emulator)) {
  all_sols = Encoding::ParseSolutions(solution_file);
  CHECK(!all_sols.empty()) << solution_file;
  CHECK(emu.get() != nullptr);
}

// XXX
[[maybe_unused]]
static void Screenshot(const Emulator &emu, const std::string &filename) {
  std::vector<uint8> save = emu.SaveUncompressed();
  std::unique_ptr<Emulator> clone(Emulator::Create("tetris.nes"));
  CHECK(clone.get() != nullptr);
  clone->LoadUncompressed(save);
  clone->StepFull(0, 0);

  ImageRGBA img(clone->GetImage(), 256, 256);
  img.Save(filename);
  printf("Wrote %s\n", filename.c_str());
}


[[maybe_unused]]
static std::string StateWithNext(RNGState s) {
  // XXX Careful. Every frame calls NextRNG, and then on drop frames
  // we may call it a second time.
  return StringPrintf("%s -> %02x",
                      RNGString(s).c_str(),
                      NextPiece(s).last_drop);
}

// We generally need to make many attempts to get the desired
// next piece, because it is RNG-dependent. The first time
// through, this  XXX docs
struct RetryState {

  RetryState(Emulator *emu,
             vector<uint8> *movie,
             int64 *steps_executed,
             MovieMaker::Callbacks *callbacks) :
    emu(emu), movie(movie), steps_executed(steps_executed),
    callbacks(callbacks) {
    Save();
  }

  int Frame() const {
    return movie->size();
  }

  int FramesSinceSave() const {
    return movie->size() - save_frame;
  }
  
  uint8 LastInput() const {
    if (movie->empty()) return 0;
    else return movie->back();
  }
  
  // Save the state, once we have gotten to a new good point.
  void Save() {

    if (VERBOSE) {
      if (min_frames > 0) {
        printf("Success on %d (min_frames %d)\n", save_frame, min_frames);
        for (int i = 0; i < (int)delays_reached.size(); i++) {
          const auto &[count, rng, drop] = delays_reached[i];
          if (count > 0) {
            printf("% 5d x% 5d  %s%s\n", i, count, RNGString(rng).c_str(),
                   i == (save_frame - min_frames) ? " <---" : "");
          }
        }
      } else {
        printf("Initial save or success on first try!\n");
      }
      printf("-----\n");
    }
      
    save_frame = movie->size();
    savestate = emu->SaveUncompressed();
    retry_count = 0;
    min_frames = -1;
    delays_reached.clear();
    rng_states.clear();
  }

  void Restore() {
    movie->resize(save_frame);
    emu->LoadUncompressed(savestate);
    rng_states.clear();
    retry_count++;
  }

  // How many times have we tried dropping? If this is 0, then we
  // don't even yet know how many frames it takes, minimally.
  int retry_count = 0;

  void UpdateFrameTable(Shape desired, Shape got) {
    // Don't call this if we already succeeded!
    CHECK(desired != got);

    // Currently the emulator is in a state right after a drop.
    // So this means that we ran NextRNG (because we do that
    // every frame) and then ran NextPiece (to compute the
    // drop), which itself may run NextRNG again.
    // The current state is not useful for predicting what
    // would drop if we took longer to get here, since in that
    // case we would not run NextPiece.
    // But the end of the rng_states vector is the state right before
    // executing that drop frame.
    CHECK(!rng_states.empty()) << "Drop on save frame??";
    // ... and then run NextRNG once (since we always do) to get the
    // state for the current frame if we did not drop.
    const RNGState rng_state =
      NextRNG(rng_states.back());
    
#if 0
    const RNGState drop_rng_state = NextPiece(rng_state);
    const RNGState next_rng_state = NextRNG(rng_state);
    const RNGState emu_rng_state = GetRNG(*emu);

    printf("UpdateFrameTable desired=%02x got=%02x\n"
           "History size: %d\n"
           "RNG from history is   %s\n"
           "if dropping, then     %s\n"
           "if not dropping, then %s\n"
           "and in emu after drop %s\n",
           desired, got,
           (int)rng_states.size(),
           StateWithNext(rng_state).c_str(),
           StateWithNext(drop_rng_state).c_str(),           
           StateWithNext(next_rng_state).c_str(),
           StateWithNext(emu_rng_state).c_str());
#endif
    
    // Sanity check that we are able to predict the piece we actually
    // got. (Because on this frame the emulator actually called
    // NextRNG and then NextPiece.)
    const uint8 computed_drop = NextPiece(rng_state).last_drop;
    CHECK(computed_drop == got) <<
      StringPrintf("Computed %02x but actually dropped %02x",
                   computed_drop, got);

    // Initialize the frame table the first time we retry.
    if (retry_count == 0) {
      // Prep table of RNG states up to the horizon, if instead
      // of dropping we were to delay some number of frames.
      
      min_frames = movie->size() - save_frame;
      delays_reached.clear();

      RNGState s = rng_state;
      for (int i = 0; i < HORIZON; i++) {
        uint8 drop = NextPiece(s).last_drop;
        delays_reached.emplace_back(0, s, drop);
        s = NextRNG(s);
      }
      std::get<0>(delays_reached[0])++;
      CHECK(EqualRNG(std::get<1>(delays_reached[0]), rng_state));
      CHECK(std::get<2>(delays_reached[0]) == got);

      min_delay_movie = *movie;
    }

    // XXX
    const int offset = (movie->size() - save_frame) - min_frames;
#if 0
    printf("Retry #%d, offset %d, %s, want %02x got %02x\n",
           retry_count, offset, RNGString(rng_state).c_str(),
           desired, got);
    for (int i = 0; i < (int)delays_reached.size(); i++) {
      const auto &[count, rng, drop] = delays_reached[i];
      const bool this_one = i == (save_frame - min_frames);
      if (count > 0 || this_one) {
        printf("% 5d x% 5d  %s -> %02x %s\n",
               i, count, RNGString(rng).c_str(), drop,
               this_one ? " <---" : "");
      }
    }
#endif
    
    // Diagnostics if the next assertion fails
    if (offset < 0) {
      Screenshot(*emu, "weird.png");
      SimpleFM2::WriteInputs("weird.fm2",
                             "tetris.nes",
                             "base64:Ww5XFVjIx5aTe5avRpVhxg==",
                             *movie);
      SimpleFM2::WriteInputs("weird-min-delay.fm2",
                             "tetris.nes",
                             "base64:Ww5XFVjIx5aTe5avRpVhxg==",
                             min_delay_movie);
      printf("save_frame %d + min_frames %d = %d, but now movie->size() = %d\n",
             save_frame, min_frames, save_frame + min_frames,
             (int)movie->size());
    }
    
    CHECK(offset >= 0) << offset << " with " << min_frames;
    CHECK(offset < HORIZON) << "this is possible but it should never "
      "get this bad??";
    CHECK(offset < (int)delays_reached.size());
    std::get<0>(delays_reached[offset])++;
    const auto &[count_, rng, drop] = delays_reached[offset];
    
    CHECK(EqualRNG(rng, rng_state) && got == drop) <<
      StringPrintf("On retry %d at offset %d:\n"
                   "Expected %s -> %02x\n"
                   "But got  %s -> %02x\n",
                   retry_count, offset,
                   RNGString(rng).c_str(), drop,
                   RNGString(rng_state).c_str(), got);
  }

  // Make a step in the emulator, and save it to the movie.
  void MakeStep(uint8 input) {
    const RNGState before = GetRNG(*emu);
    rng_states.push_back(before);
    
    emu->Step(input, 0);
    (*steps_executed)++;

    #if 0
    printf("Stepped %s %s  =>  %s\n",
           SimpleFM2::InputToString(input).c_str(),
           StateWithNext(before).c_str(),
           StateWithNext(GetRNG(*emu)).c_str());
    #endif
    
    movie->push_back(input);

    if (*steps_executed % CALLBACK_EVERY_STEPS == 0) {
      if (callbacks != nullptr && callbacks->made_step) {
        callbacks->made_step(*emu, *steps_executed);
      }
    }
  }
  
  // This is only meaningfully set once retry_count > 0.
  // This is the number of frames after the Save() point that
  // it takes to drop the piece using our fastest approach
  // (put it into position and then hold 'down').
  int min_frames = 0;
  // starting at min_frames
  static constexpr int HORIZON = 10000;
  // count, rng state, predicted drop on that frame
  std::vector<std::tuple<int, RNGState, uint8_t>> delays_reached;
  // XXX: only for debugging
  vector<uint8> min_delay_movie;
  
private:
  // The point we return to, in order to retry.
  vector<uint8> savestate;
  // And the length of the movie at that point.
  int save_frame = 0;

  // Not owned.
  Emulator *emu = nullptr;
  vector<uint8> *movie = nullptr;
  int64 *steps_executed = nullptr;
  MovieMaker::Callbacks *callbacks = nullptr;
  
  // contains the rng state before executing each frame since
  // save_frame (so it is empty right after saving or restoring).
  vector<RNGState> rng_states;
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
  RetryState retry_state(emu.get(), &outmovie, &steps_executed, &callbacks);
  
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
  int pieces = 0;
  Timer run_timer;
  static constexpr int REPORT_EVERY = 10;
  int next_report = REPORT_EVERY;

  for (;;) {

    uint8 cur_byte = emu->ReadRAM(MEM_CURRENT_PIECE);
    Shape cur = (Shape)cur_byte;
    Shape next_byte = (Shape)emu->ReadRAM(MEM_NEXT_PIECE);
    Piece next = DecodePiece(next_byte);
    uint8 current_counter = emu->ReadRAM(MEM_DROP_COUNT);

    const bool is_paused = IsPaused(*emu);

    // XXX do this with callback
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

      #if 1
      if (outmovie.size() > 17058) {
        Screenshot(*emu, "stuckagain.png");
        SimpleFM2::WriteInputs("stuckagain.fm2",
                               "tetris.nes",
                               "base64:Ww5XFVjIx5aTe5avRpVhxg==",
                               outmovie);
        exit(-1);
      }
      #endif
      
      next_report = seconds + REPORT_EVERY;
    }

    if (cur_byte == 0x13) {
      // Wait for lines to finish. Not a valid piece so we don't
      // want to enter the code below.
      // TODO: tetrises?

      // Seems we can get into this state paused; so un-pause!
      const uint8 input =
        (IsPaused(*emu) &&
         0 == (retry_state.LastInput() & INPUT_T)) ?
        INPUT_T : 0;

      retry_state.MakeStep(input);
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
      // (XXX this is kind of a bad approach because we would fail
      // rng tracking assertions if we actually checked.)
      const Piece expected =
        (schedule_idx + 2 < (int)schedule.size()) ?
        DecodePiece(schedule[schedule_idx + 2].shape) :
        next;
      if (next != expected) {
        // We didn't get the expected piece.
        retry_state.UpdateFrameTable(DropShape(expected), next_byte);

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

    const Move move = schedule[schedule_idx];
    // The shape should only mismatch due to orientation.
    CHECK(DecodePiece(cur_shape) == DecodePiece(move.shape)) <<
      StringPrintf("Expect %c but have %c=%02x?",
                   PieceChar(DecodePiece(move.shape)),
                   PieceChar(DecodePiece(cur_shape)), cur_shape);

    const uint8 target_nes_x =
      move.col + ShapeXOffset(move.shape);

    const int frame = retry_state.Frame();
    // Do we have to move into position?
    uint8 input = 0;
    if (move.shape != cur_shape ||
        target_nes_x != cur_x) {

      // Always unpause if we aren't in the correct spot.
      if (is_paused && 0 == (retry_state.LastInput() & INPUT_T)) {
        // There seem to be cases where pausing and moving at
        // the same time do not successfully unpause. So if we
        // are unpausing, only do that.
        input = INPUT_T;
      } else {
        if ((frame % 2) == 0) {
          // PERF: Can rotate in the most efficient direction.
          if (move.shape != cur_shape)
            input |= INPUT_A;

          if (target_nes_x < cur_x) input |= INPUT_L;
          else if (target_nes_x > cur_x) input |= INPUT_R;
        }
      }

    } else {

      // We're in the correct position. Now we need to spend the
      // right number of frames to get the RNG into the right state
      // so that the next spawned piece is what we want.

      if (retry_state.retry_count == 0) {
        // If this is our first attempt, drop the piece as fast
        // as we can. We do this to establish the minimum frame,
        // from which we can try a target frame.

        // Fastest is to unpause, if we are paused.
        if (is_paused && 0 == (retry_state.LastInput() & INPUT_T)) {
          input = INPUT_T;
        } else {
          // And fastest is to hold D, with the exception that if we
          // were already holding D, we need to release for at least
          // one frame, as the game will not interpret this as fast
          // drop if we keep it held between drops. This only happens
          // if the piece was already in the correct orientation and
          // column (so we skip the above) AND we were coincidentally
          // holding D at the end of the previous drop, but the simplest
          // thing is to not consider D for the very first frame after
          // saving.

          if (retry_state.FramesSinceSave() > 0)        
            input |= INPUT_D;
        }
          
      } else {
        // XXX!
        input = 0;

        // Hold down D, but wait a little longer on each retry.
        if (retry_state.FramesSinceSave() > retry_state.retry_count)
          input |= INPUT_D;

        // XXX might want to only pause, if pausing
        // If we seem to be getting stuck, spam start to pause as well.

        // Always try unpausing
        if (retry_state.retry_count > 64) {
          input |= (rc.Byte() & INPUT_T);
        } else {
          // always unpause if we are on early retries
          if (is_paused && 0 == (retry_state.LastInput() & INPUT_T)) {
            input = INPUT_T;
          }
        }
      }
        
    }

    retry_state.MakeStep(input);
  }
}
