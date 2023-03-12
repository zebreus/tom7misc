
#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <ctype.h>
#include <thread>
#include <tuple>

#include "chess.h"
#include "pgn.h"
#include "bigchess.h"
#include "network.h"
#include "clutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "image.h"
#include "util.h"
#include "timer.h"
#include "periodically.h"
#include "ansi.h"

#include "nnchess.h"

using namespace std;

using int64 = int64_t;
using uint64 = uint64_t;
using uint8 = uint8_t;

struct EvalChess {
  static constexpr int NUM_THREADS = 6;

  // XXX according to database.lichess.org, 2020-07 and 2020-08 have
  // some incorrect evaluations
  static constexpr const char *EVAL_PGN =
    "d:\\chess\\lichess_db_standard_rated_2020-09.pgn";

  static constexpr int EVAL_POSITIONS = 100000;

  ExamplePool *example_pool = nullptr;
  EvalChess() {
    example_pool = new ExamplePool;
    example_pool->PopulateExamples(EVAL_PGN, EVAL_POSITIONS);
  }

  // Runs on CPU.
  // Returns average L1 loss, fraction with correct sign
  std::pair<double, double> Evaluate(const Network &net) {
    example_pool->WaitForAll();
    // Evan though we should be exclusive now...
    MutexLock ml(&example_pool->pool_mutex);

    // expected, actual
    Timer predict_timer;
    std::vector<std::pair<float, float>> errs =
      ParallelMap(example_pool->pool,
                  [&](const std::tuple<Position, float, float> &ex) {
                    const auto &[pos, score, over] = ex;

                    float target_score = 0.0/0.0;
                    Stimulation stim(net);

                    // We train only on white-to-move, so flip the
                    // board (and the eval) if it's black's move.
                    if (pos.BlackMove()) {
                      Position rev = Position::FlipSides(pos);
                      NNChess::BoardVecTo(rev, &stim.values[0], 0);
                      target_score = -score;
                    } else {
                      NNChess::BoardVecTo(pos, &stim.values[0], 0);
                      target_score = score;
                    }

                    net.RunForward(&stim);

                    return make_pair(target_score, stim.values.back()[0]);
                  },
                  NUM_THREADS);
    const double predict_sec = predict_timer.Seconds();

    printf("Evaluated %d positions in %.2fs (%.2f p/s)\n",
           EVAL_POSITIONS, predict_sec, EVAL_POSITIONS / predict_sec);

    int correct = 0;
    double total_l1 = 0.0;
    for (const auto &[a, e] : errs) {
      total_l1 += fabs(a - e);
      if (a < 0.0 && e < 0.0) correct++;
      else if (a > 0.0 && e > 0.0) correct++;
      else if (a == 0.0 && e == 0.0) correct++;
    }
    const double avg_l1 = total_l1 / errs.size();
    const double frac_correct = correct / (double)errs.size();

    printf("Average loss (L1): %.6f\n", avg_l1);
    printf("Correct sign: %.3f%%\n", frac_correct * 100.0);
    return make_pair(avg_l1, frac_correct);
  }
};
