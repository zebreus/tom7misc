#ifndef _GRAD_NNCHESS_H
#define _GRAD_NNCHESS_H

#include "network-gpu.h"

#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <ctype.h>
#include <chrono>
#include <thread>
#include <deque>
#include <numbers>
#include <tuple>
#include <unordered_map>

#include "chess.h"
#include "pgn.h"
#include "bigchess.h"
#include "network.h"
#include "network-test-util.h"
#include "clutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "image.h"
#include "util.h"
#include "train-util.h"
#include "error-history.h"
#include "timer.h"
#include "periodically.h"

struct NNChess {

  using int64 = int64_t;
  using uint64 = uint64_t;
  using uint8 = uint8_t;

  // 8x8x13 one-hot, then 4x castling bits,
  // 8x en passant state
  static constexpr int SQUARE_SIZE = 13;
  static constexpr int BOARD_SIZE = 8 * 8 * SQUARE_SIZE + 4 + 8;
  static constexpr int OTHER_STATE_IDX = 8 * 8 * SQUARE_SIZE;
  static constexpr int OTHER_STATE_SIZE = 4 + 8;

  static constexpr int INPUT_SIZE = BOARD_SIZE;
  // Just the evaluation.
  static constexpr int OUTPUT_SIZE = 1;

  // Maps eval scores to [-1, +1]. We try to use the dynamic range.
  // The most interesting finite scores are in like [-12,12] pawns;
  // beyond that is "completely winning" stuff, so we don't care
  // as much about precision.
  // So we have three ranges (on positive and negative side):
  // Pawn scores in [0,12] map linearly to [0,0.75].
  // Pawn scores beyond that map from 0.75 to 0.90.
  // Mate maps from 0.9 to 1.0.

  static float MapEval(const PGN::Eval &eval) {
    const double signed_score = (eval.type == PGN::EvalType::MATE) ?
      (double)eval.e.mate : eval.e.pawns;
    const double sign = signed_score >= 0.0 ? 1.0 : -1.0;
    const double score = fabs(signed_score);

    switch (eval.type) {
    case PGN::EvalType::PAWNS: {
      if (score > 12.0) {
        // There's no actual limit, but we treat 64 as the
        // largest possible score.
        float f = std::clamp((std::min(score, 64.0) - 12.0) / (64.0 - 12.0),
                             0.0, 1.0);
        return sign * (0.75 + f * 0.15);
      } else {
        return sign * score * (0.75 / 12.0f);
      }
    }
    case PGN::EvalType::MATE: {
      // Not really any limit to the depth of a mate we could
      // discover, so this asymptotically approaches 0. A mate
      // in 1 has the highest bonus, 0.10f.

      static constexpr float MAX_BONUS = 0.10f;
      const double depth = score - 1.0;

      // This scale is abitrary; we just want something that
      // considers mate in 5 better than mate in 6, and so on.
      static constexpr float SCALE = 0.1f;
      float depth_bonus =
        (1.0f - (1.0f / (1.0f + expf(-SCALE * depth)))) * 2.0f * MAX_BONUS;
      return sign * (0.9f + depth_bonus);
    }

    default:
      CHECK(false) << "Bad/unimplemented eval type";
      return 0.0f;
    }
  }

  // Write into the output vector starting at the given index; the space must
  // already be reserved.
  //
  // For this experiment, the board must have white to move. Caller should
  // flip the position if it's black's move.
  static void BoardVecTo(const Position &pos,
                         std::vector<float> *out, int idx) {
    // Flip the board first.
    CHECK(!pos.BlackMove());

    for (int i = 0; i < BOARD_SIZE; i++) (*out)[idx + i] = 0.0f;

    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        const uint8 p = pos.SimplePieceAt(y, x);
        if (p == Position::EMPTY) {
          (*out)[idx] = 1.0f;
        } else if ((p & Position::COLOR_MASK) == Position::WHITE) {
          const uint8 t = p & Position::TYPE_MASK;
          (*out)[idx + 1 + t] = 1.0f;
        } else {
          // p & COLOR_MASK == BLACK
          const uint8 t = p & Position::TYPE_MASK;
          (*out)[idx + 1 + 6 + t] = 1.0f;
        }
        idx += 13;
      }
    }

    // Castling.
    (*out)[idx++] = pos.CanStillCastle(false, false) ? 1.0f : 0.0f;
    (*out)[idx++] = pos.CanStillCastle(false, true) ? 1.0f : 0.0f;
    (*out)[idx++] = pos.CanStillCastle(true, false) ? 1.0f : 0.0f;
    (*out)[idx++] = pos.CanStillCastle(true, true) ? 1.0f : 0.0f;
    // En passant state.
    std::optional<uint8> ep = pos.EnPassantColumn();
    if (ep.has_value()) {
      uint8 c = ep.value() & 0x7;
      (*out)[idx + c] = 1.0f;
    }
  }


};

struct ExamplePool {
  // Loads example positions from a database in the background.
  std::mutex pool_mutex;
  // position, eval, over.
  std::vector<std::tuple<Position, float, float>> pool;

  void PopulateExamplesInBackground(
      const std::string &filename,
      int64 max_positions) {
    // XXX leaks
    (void) new std::thread(&ExamplePool::PopulateExamples,
                           this,
                           filename,
                           max_positions);
  }

  void PopulateExamples(
      std::string filename,
      int64 max_positions) {
    PGNParser parser;
    printf("Loading examples from %s...\n", filename.c_str());
    int64 num_games = 0, num_positions = 0, parse_errors = 0;
    PGNTextStream ts(filename.c_str());
    std::string pgn_text;
    while (ts.NextPGN(&pgn_text)) {
      PGN pgn;
      if (parser.Parse(pgn_text, &pgn)) {
        if (!pgn.moves.empty() && pgn.moves[0].eval.has_value()) {
          Position pos;
          for (int i = 0; i < pgn.moves.size(); i++) {
            const PGN::Move &m = pgn.moves[i];

            Position::Move move;
            if (!pos.ParseMove(m.move.c_str(), &move)) {
              parse_errors++;
              goto next_game;
            }

            pos.ApplyMove(move);

            // Eval applies to position after move.
            if (m.eval.has_value()) {
              const float score = NNChess::MapEval(m.eval.value());
              const float over = i / (float)pgn.moves.size();

              {
                MutexLock ml(&pool_mutex);
                pool.emplace_back(pos, score, over);
              }
              num_positions++;


              if (max_positions >= 0 && num_positions >= max_positions)
                goto done;
            }
          }
          num_games++;
          if (num_games % 10000 == 0) {
            printf("Read %lld games, %lld pos from %s\n",
                   num_games, num_positions, filename.c_str());
          }
        }
      } else {
        parse_errors++;
      }
    next_game:;
    }

  done:
    printf("Done loading examples from %s.\n"
           "%lld games, %lld positions, %lld errors\n",
           filename.c_str(), num_games, num_positions, parse_errors);
  }
};

#endif
