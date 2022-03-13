
#ifdef __MINGW32__
#include <windows.h>
#undef ARRAYSIZE
#endif

#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cmath>
#include <unordered_set>
#include <bit>
#include <utility>
#include <functional>
#include <mutex>
#include <cstdio>
#include <map>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "image.h"
#include "timer.h"
#include "threadutil.h"
#include "randutil.h"
#include "util.h"

#include "tetris.h"
#include "encoding.h"

static constexpr int MAX_PARALLELISM = 24;
static constexpr bool ENDLESS = true;

// TODO: Console stuff to cc-lib
// Cursor to beginning of previous line
#define ANSI_PREVLINE "\x1B[F"
#define ANSI_CLEARLINE "\x1B[2K"
#define ANSI_CLEARTOEOL "\x1B[0K"

#define ANSI_RED "\x1B[1;31;40m"
#define ANSI_GREY "\x1B[1;30;40m"
#define ANSI_BLUE "\x1B[1;34;40m"
#define ANSI_CYAN "\x1B[1;36;40m"
#define ANSI_YELLOW "\x1B[1;33;40m"
#define ANSI_GREEN "\x1B[1;32;40m"
#define ANSI_WHITE "\x1B[1;37;40m"
#define ANSI_PURPLE "\x1B[1;35;40m"
#define ANSI_RESET "\x1B[m"

#define ANSI_HIDE_CURSOR "\x1B[?25h"
#define ANSI_SHOW_CURSOR "\x1B[?25l"

// Synchronous update
// XXX they don't work?
#define MINTTY_SYNCHRONOUS_START "\x1BP=1s\x1B\\"
#define MINTTY_SYNCHRONOUS_END "\x1BP=2s\x1B\\"

// Same as printf, but using WriteConsole on windows so that we
// can communicate with pseudoterminal. Without this, ansi escape
// codes will work (VirtualTerminalProcessing) but not mintty-
// specific ones.
// TODO: It would be better if we had a way of stripping the
// terminal codes if they are not supported?
static void CPrintf(const char* format, ...) {
  // Do formatting.
  va_list ap;
  va_start(ap, format);
  string result;
  StringAppendV(&result, format, ap);
  va_end(ap);

  #ifdef __MINGW32__
  DWORD n = 0;
  WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE),
               result.c_str(),
               result.size(),
               &n,
               nullptr);
  #else
  printf("%s", result.c_str());
  #endif
}


using namespace std;
using uint8 = uint8_t;
using uint16 = uint16_t;

using Tetris = TetrisDepth<6>;

// All right, trying again!

// Current approach: For each bit pattern, find a
// sequence that encodes that bit pattern from the start
// state (without touching or depending on anything
// below) and then restores the start state (shifted
// up one line).
//
// One immediate observation is that we can't achieve
// this with any fixed LHS prefix. Note that the tetris
// field always contains an even number of 1 bits,
// because each piece sets 4 bits and then removes n *
// 10 lines, both of which are even. We can instead use
// the leftmost bit as a parity bit, although this fails
// for 0xFF, since we would then create a line. The
// simplest diff on the idea would be to use a prefix of
// 00 in (just) this case, so let's try that.

// Awesome... first try solves all patterns in seconds
// each (typically ~25 pieces), including 0xFF.
// MAX_HEIGHT=8, and we might even be able to shrink
// this, which would probably yield better solutions?
//
// Clearing is harder (perhaps the heuristic just stinks,
// or maybe the effectively smaller playfield) but still
// very tractable.

constexpr int REPORT_EVERY = 10;

[[maybe_unused]]
static vector<Move> Encode(uint8 target,
                           const std::function<void(int)> &Phase1Callback,
                           int current_solution,
                           bool loud = false) {
  const uint16 full_target = Encoding::FullTarget(target);

  // Be actually random if ENDLESS mode is on, otherwise we'll just keep
  // producing the same solution.
  ArcFour rc(StringPrintf("%02x.%lld", target,
                          ENDLESS ? time(nullptr) : 0));

  // If set, only play the first move of the best sequence, then replan.
  // This is probably better, but slower -- otherwise we require the
  // first phase to be completed in a multiple of 3 pieces, for example.
  static constexpr bool REPLAN_AFTER_ONE = true;

  static constexpr int PHASE1_SEARCH_DEPTH = ENDLESS ? 4 : 3;
  static constexpr int PHASE2_SEARCH_DEPTH = ENDLESS ? 4 : 3;  
  
  // Just in endless mode.
  static constexpr int MAX_TOTAL_MOVES = 250;
  int total_moves = 0;

  // Intial setup is
  //
  // #.........
  // ##........
  // .#........ <- target row
  // ??????????
  //
  // Our goal is to create
  // #.........
  // ##........
  // .#........
  // ppbbbbbbbb <- target row
  // ??????????
  //
  // Where bb are the 8 bits of the target and pp is the
  // appropriate fixed prefix. (Once we reach this
  // state, we're now set to work on the next target
  // row.) In doing this we aren't allowed to touch or
  // depend on the contents of ??, and so this will also
  // preserve anything below our work area. We don't
  // even represent it, in fact, so our starting setup
  // is just
  //
  // #.........
  // ##........
  // .#........ <- target row
  //
  // with the last piece set to S.


  // Since we can't touch the "bottom," the main way we
  // will make progress is to place pieces that hang off
  // of the "pile", which is whatever we build on top of
  // that starting S. For example, we can place a 7:
  //
  // #oo.......
  // ##o.......
  // .#o.......
  //
  // which sets the high bit, or a Z and then a 7:
  //
  // #oo.......
  // ##oo......
  // .#........
  //
  // #oo##.....
  // ##oo#.....
  // .#..#.....
  //
  // which sets 001. However, both have removed the immediate
  // usefulness of the pile, as no more pieces can reach into
  // the target line. So then we have to do some cleanup to
  // shrink it. Cleaning up requires full lines, so we need
  // to stretch across the playfield, e.g. with horiz I and J
  // pieces.
  //
  // ..........         .......###               .......oo.
  // ....====..         ....====.#   (clear      ....====o#
  // #oo##.....   -->   #oo##.....    top line   #oo##...o.
  // ##oo#.....         ##oo#.....    somehow)   ##oo#.....
  // .#..#.....         .#..#.....               .#..#.....
  //
  // Note the utility of the hole and the floating piece we
  // create here, as it will let us hang more 7s and Fs to
  // get pieces closer to the target row.
  //
  // Note that the starting setup is already wrong in
  // the bottom-left unless the parity is 01. So we may
  // need to clear part of the pile in order to get the
  // correct parity. Note the special case of 0xFF
  // requiring a parity of 0b00 requires us to clear
  // this before establishing the remainder of the bits,
  // since we cannot clear a bit without clearing the
  // entire row. This will probably require special
  // treatment?
  //
  // Once we get to the point where the target row has the
  // correct pattern, we need to clean up any leftover
  // stuff (we can use the pattern we established as a base,
  // but we also need to be able to do this for 0x00!) and
  // place the initial S. Let's work on this second phase
  // later.

  // The move space is not that big; we have 19 shapes (but
  // can't repeat pieces in a row) in 10 or fewer columns.
  // So we could exhaustively search 2 or 3 ply, and can
  // probably go deeper if we pick favorite shapes and
  // avoid some probably-bad patterns. So let's start with
  // search using some objective function.
  //
  // The objective function is hierarchical.
  //  - If we can set a bit in the target row that should be
  //    1 and is not, always do that.
  //  - If the target row has bits set where it should not
  //    (it needs to be cleared), and we can set any bit,
  //    do so. (Probably needs some caveat for completing
  //    the line, because we don't want to drop something
  //    incorrect into the line.)

  // TODO(perf): Can pretty straightforwardly hyper-optimize
  // the weights here.

  std::function<double(const Tetris &)> EvaluateSetup =
    [full_target](const Tetris &tetris) {
      const uint16 last_line = tetris.rows[Tetris::MAX_DEPTH - 1];
      // TODO: clean up in this case. But probably we should
      // switch to a different strategy!
      if (last_line == full_target)
        return 1'000'000'000.0;

      // bits incorrectly set
      const uint16 incorrect = last_line & ~full_target;
      // bits not yet set
      const uint16 todo = ~last_line & full_target;

      double score = 0.0;
      if (incorrect != 0) {
        // This is very bad because we need to clear the line.
        score -= 1'000'000.0;
        // but in this case we want the line to be more full
        // regardless.
        score += 50'000.0 * std::popcount(last_line);

        // TODO: penalize the row above this one for having incorrect
        // bits. It will become the target row when we finish the
        // line, so if it has bad bits, we'll just have to do it
        // again.
        const uint16 second_last_line =
          tetris.rows[Tetris::MAX_DEPTH - 2];
        const uint16 second_last_incorrect =
          second_last_line & ~full_target;

        score -= 25'000.0 * std::popcount(second_last_incorrect);
        
      } else {
        // Penalize for missing bits.
        score += -50'000.0 * std::popcount(todo);
      }

      // Simplest other heuristic here: Good to have
      // bits set in rows close to the target line. This
      // is the thing that's most challenging about the
      // problem; needing to put these on overhangs from
      // the pile on the left.
      int dist = 0;
      for (int r = Tetris::MAX_DEPTH - 2; r >= 0; r--) {
        const uint16_t row = tetris.rows[r];
        const float mult =
          // good to have bits
          (dist == 0) ? 1'000.0 :
          (dist == 1) ? 10.0 :
          // no longer good
          (dist == 2) ? -10 :
          dist * -100;
        // Probably should score these differently if
        // they cover correct bits vs. incorrect. Or
        // perhaps generalize what we do for the above:
        // Does this line need to be cleared or not?
        score += std::popcount(row) * mult;
        dist++;
      }

      return score;
    };

  // To clear, it's the same idea, but with three target rows
  // instead of one.
  std::function<double(const Tetris &)> EvaluateClear =
    [full_target](const Tetris &tetris) {
      // Don't ever touch the completed line!
      // (We should probably just cut this off during the search
      // procedure?)
      {
        const uint16 notouch_line = tetris.rows[Tetris::MAX_DEPTH - 1];
        if (notouch_line != full_target)
          return -1'000'000'000.0;
      }

      const uint16 last_line1 = tetris.rows[Tetris::MAX_DEPTH - 4];
      const uint16 last_line2 = tetris.rows[Tetris::MAX_DEPTH - 3];
      const uint16 last_line3 = tetris.rows[Tetris::MAX_DEPTH - 2];

      double score = 0.0;
      if (last_line1 == Encoding::STDPOS1 &&
          last_line2 == Encoding::STDPOS2 &&
          last_line3 == Encoding::STDPOS3) {
        // Has the standard position but we also don't want
        // anything above it. We also have to end with one of
        // the allowed pieces.
        int blank_prefix = 0;
        for (int r = 0; r < Tetris::MAX_DEPTH; r++) {
          if (tetris.rows[r] != 0) {
            break;
          }
          blank_prefix++;
        }

        if (blank_prefix == Tetris::MAX_DEPTH - 4 &&
            Encoding::IsOkToEnd(tetris.GetLastPiece())) {
          // Done!
          return 1'000'000'000.0;
        } else {
          // Still considered good, but we need to clear
          // the junk above or waste moves to end with
          // an allowed piece.
          score += 1'000'000.0;
        }
      }

      // Otherwise, points for as many lines as we currently have
      // correct, working from the bottom.
      #if 0
      if (last_line1 == Encoding::STDPOS1) {
        score += 10'000'000.0;
        if (last_line2 == Encoding::STDPOS2) {
          score += 10'000'000.0;
          // (we know we don't have all three...)
        }
      }
      #endif

      // Since we know the standard position can be made with a
      // single piece, we actually just favor an empty board here,
      // since if we get that we will be done.

      // TODO: this tries to create a three-high wall and clear it.
      int dist = 0;
      uint16_t prev_row = 0b1111111111;
      for (int r = Tetris::MAX_DEPTH - 2; r >= 0; r--) {
        const uint16_t row = tetris.rows[r];
        const float mult =
          // good to have bits
          (dist == 0) ? 1'000.0 :
          (dist == 1) ? 20.0 :
          // ??
          (dist == 2) ? -10 :
          // bad
          dist * -100;
        score += std::popcount(row) * mult;

        // covered hole is a bit in this row that was not
        // set in the previous.
        uint16_t covered_hole = row & ~prev_row;

        score += -500.0 * std::popcount(covered_hole);

        prev_row = row;
        dist++;
      }

      return score;
    };

  std::vector<Shape> ALL_SHAPES = {
    I_VERT, I_HORIZ,
    SQUARE,
    T_UP, T_DOWN, T_LEFT, T_RIGHT,
    J_UP, J_LEFT, J_DOWN, J_RIGHT,
    Z_HORIZ, Z_VERT,
    S_HORIZ, S_VERT,
    L_UP, L_LEFT, L_DOWN, L_RIGHT,
  };

  // Shapes we can use on the very first move. This is
  // anything that is not allowed as an ending shape.
  // (Note the check for repeating S is also covered
  // with GetLastPiece below, but we may not have
  // actually played an S to create that shape.)
  std::vector<Shape> ALL_STARTING_SHAPES;
  for (Shape s : ALL_SHAPES) {
    if (!Encoding::IsOkToEnd(DecodePiece(s))) {
      ALL_STARTING_SHAPES.push_back(s);
    }
  }

  std::vector<int> ALL_COLS;
  for (int i = 0; i < 10; i++) ALL_COLS.push_back(i);

  // XXX

  Shuffle(&rc, &ALL_SHAPES);
  Shuffle(&rc, &ALL_COLS);


  // Get the best scoring sequence of moves, with the score.
  std::function<pair<vector<Move>, double>(
      const std::vector<Shape> &,
      const std::function<double(const Tetris&)> &,
      const Tetris &,
      int, bool)> GetBestRec =
    [full_target, &ALL_SHAPES, &ALL_COLS, &GetBestRec](
        // Allowed shapes for the FIRST ply. Later plies use all shapes.
        const std::vector<Shape> &allowed_shapes,
        const std::function<double(const Tetris&)> &Evaluate,
        const Tetris &start_tetris,
        int num_moves, bool no_unsolve) ->
    pair<vector<Move>, double> {
    if (num_moves == 0) {
      return make_pair(std::vector<Move>{}, Evaluate(start_tetris));
    } else {
      // For now, exhaustive.
      // TODO: Consider fewer shapes as we search more deeply.
      // TODO: Explore these in a random order; even when exhaustive
      // we may break ties?

      std::vector<Move> best_rest;
      double best_rest_score = -1.0/0.0;
      for (Shape shape : allowed_shapes) {
        // Avoid inner loop if this would repeat.
        if (start_tetris.GetLastPiece() != DecodePiece(shape)) {
          for (int col : ALL_COLS) { // = 0; col < 10; col++
            Tetris tetris = start_tetris;
            if (tetris.Place(shape, col)) {
              if (no_unsolve &&
                  tetris.rows[Tetris::MAX_DEPTH - 1] != full_target) {
                // Don't allow perturbing the already solved part at
                // any step.
                continue;
              }
              // Recurse.
              const auto &[rest, rest_score] =
                GetBestRec(
                    // Always allow all shapes in recursive calls,
                    // since we only use this arg to ban ending
                    // shapes on the very first move.
                    ALL_SHAPES,
                    Evaluate, tetris, num_moves - 1, no_unsolve);
              if (rest_score > best_rest_score) {
                best_rest.clear();
                best_rest.emplace_back(shape, col);
                for (Move m : rest) best_rest.push_back(m);
                best_rest_score = rest_score;
              }
            }
          }
        }
      }
      return make_pair(std::move(best_rest), best_rest_score);
    }
  };

  Tetris tetris;
  CHECK(Encoding::IsOkToEnd(tetris.GetLastPiece())) << "The starting "
    "state must be one of the allowed ending pieces, and this "
    "additionally needs to be enforced elsewhere! Got: " <<
    PieceChar(tetris.GetLastPiece());

  std::vector<Move> movie;

  auto Restart1 = [&]() {
      Shuffle(&rc, &ALL_SHAPES);
      Shuffle(&rc, &ALL_COLS);
      tetris = Tetris();
      movie.clear();
      // Prevent Byzantine nontermination.
      total_moves++;
      if (loud) {
        printf("Restarted!\n");
      }
    };

  // Phase 1: Set the target line.
  for (;;) {
    const uint16 last_line = tetris.rows[Tetris::MAX_DEPTH - 1];
    if (last_line == full_target) {
      break;
    }

    if (ENDLESS && total_moves > MAX_TOTAL_MOVES) {
      return {};
    }

    // TODO: tune cutoff?
    if (movie.size() > 50 ||
        (ENDLESS && movie.size() > (current_solution * 0.75))) {
      Restart1();
      continue;
    }

    // On the very first move, we must not use a shape that may
    // have ended the previous encoding. Otherwise, whatever you want
    // (direct repeats are enforced by the Tetris object).
    const std::vector<Shape> *allowed_shapes =
      movie.empty() ? &ALL_STARTING_SHAPES : &ALL_SHAPES;
    const int search_depth = 1 + RandTo(&rc, PHASE1_SEARCH_DEPTH);
    const auto &[moves, score_] = GetBestRec(
        *allowed_shapes, EvaluateSetup, tetris, search_depth, false);
    if (moves.empty()) {
      Restart1();
      continue;
    }
    for (const Move &m : moves) {
      CHECK(tetris.Place(m.shape, m.col));
      movie.push_back(m);
      total_moves++;
      if (REPLAN_AFTER_ONE)
        break;
    }
  }

  Phase1Callback(movie.size());
  if (loud) {
    printf("Phase 1 done in %d:\n%s\n", (int)movie.size(),
           tetris.BoardString().c_str());
  }

  const Tetris backup_tetris = tetris;
  const std::vector<Move> backup_movie = movie;

  auto Restart2 = [&]() {
      Shuffle(&rc, &ALL_SHAPES);
      Shuffle(&rc, &ALL_COLS);
      tetris = backup_tetris;
      movie = backup_movie;
      // Prevent Byzantine nontermination.
      total_moves++;
      if (loud) {
        printf("Restarted!\n");
      }
    };

  // Phase 2: Put in standard position.
  for (;;) {

    // Are we done?
    int blank_prefix = 0;
    for (int r = 0; r < Tetris::MAX_DEPTH; r++) {
      if (tetris.rows[r] != 0) {
        break;
      }
      blank_prefix++;
    }

    // Must not have anything above the STDPOS.
    if (blank_prefix == Tetris::MAX_DEPTH - 4 &&
        // and must use an allowed ending piece
        Encoding::IsOkToEnd(tetris.GetLastPiece())) {
      const uint16 last_line1 = tetris.rows[Tetris::MAX_DEPTH - 4];
      const uint16 last_line2 = tetris.rows[Tetris::MAX_DEPTH - 3];
      const uint16 last_line3 = tetris.rows[Tetris::MAX_DEPTH - 2];
      if (last_line1 == Encoding::STDPOS1 &&
          last_line2 == Encoding::STDPOS2 &&
          last_line3 == Encoding::STDPOS3) {
        CHECK(tetris.rows[Tetris::MAX_DEPTH - 1] == full_target) << "oops";

        Tetris replay;
        for (Move m : movie) {
          CHECK(replay.Place(m.shape, m.col));
        }

        const uint16 last_line1 = replay.rows[Tetris::MAX_DEPTH - 4];
        const uint16 last_line2 = replay.rows[Tetris::MAX_DEPTH - 3];
        const uint16 last_line3 = replay.rows[Tetris::MAX_DEPTH - 2];

        CHECK(replay.rows[Tetris::MAX_DEPTH - 1] == full_target &&
              last_line1 == Encoding::STDPOS1 &&
              last_line2 == Encoding::STDPOS2 &&
              last_line3 == Encoding::STDPOS3) << "Supposed solution "
          "for " << (int)target << " actually made board:\n" <<
          replay.BoardString() <<
          " " << RowString(full_target) << " <- target";

        return movie;
      }
    }

    if (ENDLESS && total_moves > MAX_TOTAL_MOVES) {
      return {};
    }

    if (movie.size() > 100 ||
        (ENDLESS && (int)movie.size() >= current_solution)) {
      Restart2();
      continue;
    }

    // Technically the first phase could produce an empty movie, so
    // we need the same caveat about the starting piece here, too.
    const std::vector<Shape> *allowed_shapes =
      movie.empty() ? &ALL_STARTING_SHAPES : &ALL_SHAPES;

    // In endless mode, don't bother planning beyond our current
    // record. This gives us a speedup in the case where we fail,
    // which is the most common in the steady state.
    const int max_useful_depth = ENDLESS ?
      current_solution - movie.size() :
      // doesn't matter; gets clamped
      100;
    const int use_depth = std::clamp(
        max_useful_depth, 1,
        1 + (int)RandTo(&rc, PHASE2_SEARCH_DEPTH));
    const auto &[moves, score_] = GetBestRec(
        *allowed_shapes, EvaluateClear, tetris, use_depth, true);
    if (moves.empty()) {
      /*
        CHECK(!moves.empty()) << "No valid moves here (after "
        << movie.size() << " played):\n"
        << tetris.BoardString();
      */
      // Stuck :(
      Restart2();
    } else {
      for (const Move &m : moves) {
        CHECK(tetris.Place(m.shape, m.col));
        movie.push_back(m);
        total_moves++;
        if (REPLAN_AFTER_ONE)
          break;
      }
    }
  }

}

[[maybe_unused]]
static void PrintSolution(uint8 target,
                          const std::vector<Move> &movie, bool loud) {
  const uint16 full_target = Encoding::FullTarget(target);
  printf("Done in %d moves!\n", (int)movie.size());

  // Replay.
  Tetris replay;
  for (Move m : movie) {
    if (loud) {
      printf("Place %c at %d:\n%s", PieceChar(DecodePiece(m.shape)), m.col,
             replay.BoardString().c_str());
    }
    CHECK(replay.Place(m.shape, m.col));
  }

  printf("Enc %02x. Final board:\n%s %s <- target\n",
         target,
         replay.BoardString().c_str(),
         RowString(full_target).c_str());
  return;
}

static constexpr const char *solsfile = "solutions.txt";
static void AppendSolution(int idx, const std::vector<Move> &movie,
                           double seconds) {
  FILE *f = fopen(solsfile, "ab");
  CHECK(f != nullptr);
  fprintf(f, "%02x %s %.2fsec\n", idx,
          Encoding::MovieString(movie).c_str(),
          seconds);
  fclose(f);
}

static void SolveAll() {
  for (int i = 0; i < 38; i++) CPrintf("\n");

  const std::map<uint8, std::vector<Move>> startsols =
    Encoding::ParseSolutions(solsfile);

  std::mutex m;
  std::vector<bool> failed(256, false);
  std::vector<bool> done(256, false);
  std::vector<bool> working(256, false);
  std::vector<int> status(256, -2);
  for (const auto &[idx, movie] : startsols) {
    done[idx] = true;
    status[idx] = movie.size();
  }

  auto PrintTable = [&m, &failed, &done, &status]() {
      MutexLock ml(&m);

      constexpr int STATUS_LINES = 32;
      CPrintf("%s", MINTTY_SYNCHRONOUS_START ANSI_HIDE_CURSOR);
      for (int i = 0; i < STATUS_LINES; i++) {
        CPrintf("%s", ANSI_PREVLINE);
      }

      static_assert(256 % STATUS_LINES == 0);
      constexpr int STATUS_COLS = 256 / STATUS_LINES;
      for (int y = 0; y < STATUS_LINES; y++) {
        for (int x = 0; x < STATUS_COLS; x++) {
          int idx = y * STATUS_COLS + x;
          int s = status[idx];
          bool f = failed[idx];
          bool d = done[idx];

          const char *color =
            f ? ANSI_RED :
            d ? ANSI_GREEN :
            s == -1 ? ANSI_BLUE :
            s == -2 ? ANSI_YELLOW :
            ANSI_PURPLE;
          CPrintf(ANSI_WHITE "%02x" ANSI_GREY ":%s% 4d" ANSI_RESET "  ",
                  idx, color, status[idx]);
        }
        CPrintf(ANSI_CLEARTOEOL "\n");
      }
      CPrintf("%s", MINTTY_SYNCHRONOUS_END ANSI_SHOW_CURSOR);
    };

  ParallelComp(256,
               [&m, &failed, &done, &status, &PrintTable](int idx) {
                 {
                   MutexLock ml(&m);
                   // From solutions file.
                   if (done[idx]) return;
                   status[idx] = -1;
                 }
                 PrintTable();

                 Timer timer;
                 std::function<void(int)> Phase1Callback =
                          [&m, &status, &PrintTable, idx](int phase1) {
                            WriteWithLock(&m, &status[idx], phase1);
                            PrintTable();
                          };
                 std::vector<Move> movie = Encode(idx, Phase1Callback,
                                                  9999, false);
                 if (!ENDLESS && movie.empty()) {
                   MutexLock ml(&m);
                   status[idx] = 0;
                   failed[idx] = true;
                 } else {
                   MutexLock ml(&m);
                   status[idx] = (int)movie.size();
                   done[idx] = true;
                   AppendSolution(idx, movie, timer.Seconds());
                 }
                 PrintTable();
               },
               MAX_PARALLELISM);

  int worst = 0;
  int total = 0;
  for (int count : status) {
    worst = std::max(worst, count);
    total += count;
  }

  printf("Done. Worst case %d, average %.2f\n", worst, total / 256.0);
}

static void EndlessImprove() {
  for (int i = 0; i < 38; i++) CPrintf("\n");

  const std::map<uint8, std::vector<Move>> startsols =
    Encoding::ParseSolutions(solsfile);
  CHECK(startsols.size() == 256) << "First SolveAll!";

  std::mutex m;
  // 0 = not working, 1 = phase 1, 2 = phase 2
  std::vector<int> working(256, 0);
  std::vector<bool> improved(256, false);
  std::vector<bool> skipping(256, false);
  std::vector<int> best(256, -2);
  int moves_saved = 0;
  Timer run_timer;
  for (const auto &[idx, movie] : startsols) {
    CHECK(idx >= 0 && idx < 256) << idx;
    best[idx] = movie.size();
  }

  auto PrintTable = [&m, &working, &improved, &skipping, &best,
                     &moves_saved, &run_timer]() {
      MutexLock ml(&m);

      constexpr int STATUS_LINES = 32;
      constexpr int SUMMARY_LINES = 2;
      CPrintf("%s", MINTTY_SYNCHRONOUS_START ANSI_HIDE_CURSOR);
      for (int i = 0; i < STATUS_LINES + SUMMARY_LINES; i++) {
        CPrintf("%s", ANSI_PREVLINE);
      }

      double seconds = run_timer.Seconds();
      double ipm = moves_saved / (seconds / 60.0);
      CPrintf(ANSI_BLUE "%0.1f" ANSI_RESET " sec, "
              ANSI_PURPLE "%0.3f" ANSI_RESET " imp/minute. "
              ANSI_GREEN "%d" ANSI_RESET " moves saved.\n"
              ANSI_GREY "-------------------------------------------\n",
              seconds, ipm, moves_saved);

      static_assert(256 % STATUS_LINES == 0);
      constexpr int STATUS_COLS = 256 / STATUS_LINES;
      for (int y = 0; y < STATUS_LINES; y++) {
        for (int x = 0; x < STATUS_COLS; x++) {
          int idx = y * STATUS_COLS + x;
          const char *idx_color =
            skipping[idx] ? ANSI_GREY : ANSI_WHITE;

          const char *color =
            improved[idx] ? ANSI_GREEN : ANSI_RED;

          const char *colon =
            working[idx] == 0 ? ANSI_GREY ":" :
            working[idx] == 1 ? ANSI_BLUE "=" :
            ANSI_PURPLE "@";
          CPrintf("%s%02x%s%s% 3d" ANSI_RESET "   ",
                  idx_color,
                  idx, colon, color, best[idx]);
        }
        CPrintf(ANSI_CLEARTOEOL "\n");
      }
      CPrintf("%s", MINTTY_SYNCHRONOUS_END ANSI_SHOW_CURSOR);
    };

  ArcFour rc(StringPrintf("ro-%lld", time(nullptr)));

  for (;;) {
    const uint8 random_offset = rc.Byte();

    // Skip if strictly better than the nth best result.
    // static constexpr int BEST_TO_SKIP = 256 / 2;
    static constexpr int BEST_TO_SKIP = 0;
    static_assert(BEST_TO_SKIP >= 0 && BEST_TO_SKIP < 256);

    // Skip if this many moves or fewer
    static constexpr int ABS_TO_SKIP = 17;

    // Might want to optimize 00 endlessly since it is so important?
    static constexpr bool ALLOW_SKIPPING_ZERO = true;

    // Hack: Always keep threads working by doing a huge parallel
    // comprehension but only looking at the lowest byte.
    ParallelComp(256 * 256 * 256,
                 [&m, &working, &improved, &skipping, &best,
                  &moves_saved, random_offset,
                  &PrintTable](int raw_idx) {
                   int idx = (raw_idx + random_offset) & 0xFF;
                   int cur_best = 9999;
                   {
                     MutexLock ml(&m);

                     cur_best = best[idx];

                     if (idx != 0 || ALLOW_SKIPPING_ZERO) {
                     
                       if (cur_best <= ABS_TO_SKIP) {
                         skipping[idx] = true;
                         return;
                       }

                       if (BEST_TO_SKIP > 0) {
                         // XXX this does not work when we have no
                         // solutions for some; better set BEST_TO_SKIP
                         // to zero in that case.
                         std::vector<int> sorted = best;
                         std::sort(sorted.begin(), sorted.end());

                         int cutoff_score = sorted[BEST_TO_SKIP];
                         if (cur_best < cutoff_score) {
                           skipping[idx] = true;
                           return;
                         }
                       }
                     }
                       
                     skipping[idx] = false;
                     
                     // Skip if someone is already working on it.
                     if (working[idx] != 0)
                       return;

                     working[idx] = 1;
                   }
                   PrintTable();

                   Timer timer;
                   std::function<void(int)> Phase1Callback =
                            [&m, &working, &PrintTable, idx](int phase1) {
                              // WriteWithLock(&m, &best[idx], phase1);
                              WriteWithLock(&m, &working[idx], 2);
                              PrintTable();
                            };
                   std::vector<Move> movie = Encode(idx, Phase1Callback,
                                                    cur_best, false);
                   {
                     MutexLock ml(&m);
                     working[idx] = 0;
                     int new_size = (int)movie.size();
                     if (!movie.empty() && new_size < best[idx]) {
                       improved[idx] = true;
                       moves_saved += (best[idx] - new_size);
                       best[idx] = new_size;
                       AppendSolution(idx, movie, timer.Seconds());
                     }
                   }
                   PrintTable();
                 },
                 MAX_PARALLELISM);
  }

  // XXX put in status
  /*
  int worst = 0;
  int total = 0;
  for (int count : status) {
    worst = std::max(worst, count);
    total += count;
  }
  */

}

int main(int argc, char **argv) {
  // Enable ANSI output on win32.
  #ifdef __MINGW32__
  if (!SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)) {
    LOG(FATAL) << "Unable to go to BELOW_NORMAL priority.\n";
  }
  
  HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
  // mingw headers may not know about this new flag
  static constexpr int kVirtualTerminalProcessing = 0x0004;
  DWORD old_mode = 0;
  GetConsoleMode(hStdOut, &old_mode);
  SetConsoleMode(hStdOut, old_mode | kVirtualTerminalProcessing);
  #endif

  if (ENDLESS) {
    EndlessImprove();
  } else {
    SolveAll();
  }

  return 0;
}
