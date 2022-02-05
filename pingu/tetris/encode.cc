
#ifdef __MINGW32__
#include <windows.h>
#undef ARRAYSIZE
#endif

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

#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "image.h"
#include "timer.h"
#include "threadutil.h"

#include "tetris.h"

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

using Tetris = TetrisDepth<8>;

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
// No clearing yet, though.

struct Move {
  Shape shape = I_VERT;
  uint8_t col = 0;
};

constexpr int REPORT_EVERY = 10;

static uint16 FullTarget(uint8 target) {
  const uint16 full_target = (uint16)target |
    (target == 0xFF ?
     /* special case for FF so we don't complete line */
     0b00 :
     ((std::popcount(target) & 1) ? 0b11 : 0b01)) << 8;
  CHECK((full_target & ~0b1111111111) == 0) << full_target;
  return full_target;
}

[[maybe_unused]]
static vector<Move> Encode(uint8 target) {
  const uint16 full_target = FullTarget(target);

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

  auto Evaluate = [full_target](const Tetris &tetris) {
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

        // TODO: penalize the row (all rows?) above for
        // having incorrect bits. It will become the
        // target row when we finish the line.
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

  static constexpr std::array<Shape, 19> ALL_SHAPES = {
    I_VERT, I_HORIZ,
    SQUARE,
    T_UP, T_DOWN, T_LEFT, T_RIGHT,
    J_UP, J_LEFT, J_DOWN, J_RIGHT,
    Z_HORIZ, Z_VERT,
    S_HORIZ, S_VERT,
    L_UP, L_LEFT, L_DOWN, L_RIGHT,
  };

  // Get the best scoring sequence of moves, with the score.
  std::function<pair<vector<Move>, double>(const Tetris &, int)> GetBestRec =
    [&Evaluate, &GetBestRec](const Tetris &start_tetris,
                             int num_moves) -> pair<vector<Move>, double> {
    if (num_moves == 0) {
      return make_pair(std::vector<Move>{}, Evaluate(start_tetris));
    } else {
      // For now, exhaustive.
      // TODO: Consider fewer shapes as we search more deeply.
      // TODO: Explore these in a random order; even when exhaustive
      // we may break ties?

      std::vector<Move> best_rest;
      double best_rest_score = -1.0/0.0;
      for (Shape shape : ALL_SHAPES) {
        // Avoid inner loop if this is a repeat.
        if (start_tetris.GetLastPiece() != DecodePiece(shape)) {
          for (int col = 0; col < 10; col++) {
            Tetris tetris = start_tetris;
            if (tetris.Place(shape, col)) {
              // Recurse.
              const auto &[rest, rest_score] =
                GetBestRec(tetris, num_moves - 1);
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
  std::vector<Move> movie;
  Timer searchtime;

  int next_report = REPORT_EVERY;

  for (;;) {
    const uint16 last_line = tetris.rows[Tetris::MAX_DEPTH - 1];
    if (last_line == full_target) {
      return movie;
    }

    const auto &[moves, score_] = GetBestRec(tetris, 3);
    CHECK(!moves.empty()) << "No valid moves here (after "
                          << movie.size() << " played):\n"
                          << tetris.BoardString();
    for (const Move &m : moves) {
      CHECK(tetris.Place(m.shape, m.col));
      movie.push_back(m);
    }

    int sec = searchtime.Seconds();
    if (sec > next_report) {
#if 0
      double score = Evaluate(tetris);
      double mps = movie.size() / searchtime.Seconds();
      printf("Enc %02x. Made %lld moves. %.2f moves/sec. Score %.2f:\n"
             "%s"
             " %s  <- target\n",
             target,
             movie.size(), mps, score,
             tetris.BoardString().c_str(),
             RowString(full_target).c_str());
#endif
      next_report = sec + 5;
    }
  }
}

[[maybe_unused]]
static void PrintSolution(uint8 target,
                          const std::vector<Move> &movie, bool loud) {
  const uint16 full_target = FullTarget(target);
  printf("Done in %lld moves!\n", movie.size());

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

int main(int argc, char **argv) {
  // Enable ANSI output on win32.
  #ifdef __MINGW32__
  HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
  // mingw headers may not know about this new flag
  static constexpr int kVirtualTerminalProcessing = 0x0004;
  DWORD old_mode = 0;
  GetConsoleMode(hStdOut, &old_mode);
  SetConsoleMode(hStdOut, old_mode | kVirtualTerminalProcessing);
  #endif

  for (int i = 0; i < 38; i++) CPrintf("\n");

  std::mutex m;
  std::vector<int> status(256, -2);
  auto PrintTable = [&m, &status]() {
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
          const char *color = s > 0 ? ANSI_GREEN :
            s == -1 ? ANSI_BLUE : ANSI_YELLOW;
          CPrintf(ANSI_WHITE "%02x" ANSI_GREY ":%s% 4d" ANSI_RESET "  ",
                  idx, color, status[idx]);
        }
        CPrintf(ANSI_CLEARTOEOL "\n");
      }
      CPrintf("%s", MINTTY_SYNCHRONOUS_END ANSI_SHOW_CURSOR);
    };


  ParallelComp(256,
               [&m, &status, &PrintTable](int idx) {
                 WriteWithLock(&m, &status[idx], -1);
                 PrintTable();
                 std::vector<Move> movie = Encode(idx);
                 WriteWithLock(&m, &status[idx], (int)movie.size());
                 PrintTable();
               },
               24);

  #if 0
  for (int x = 0; x < 256; x++) {
    uint8 b = x;
    if ((std::popcount(b) & 1) == 1) {
      Encode(x);
    }
  }
  #endif

  return 0;
}
