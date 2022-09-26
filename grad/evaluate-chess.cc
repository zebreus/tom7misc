
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

static constexpr int NUM_THREADS = 6;

static constexpr const char *EVAL_PGN =
  "d:\\chess\\lichess_db_standard_rated_2020-07.pgn";

static constexpr int EVAL_POSITIONS = 100000;

static void Evaluate(const Network &net) {
  // Note that destructor does not stop the thread. If we want to
  // have train exit cleanly, we'd need to add this.
  ExamplePool *example_pool = new ExamplePool;
  example_pool->PopulateExamples(EVAL_PGN, EVAL_POSITIONS);

  {
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

    double total_l1 = 0.0;
    for (const auto &[a, e] : errs)
      total_l1 += fabs(a - e);
    const double avg_l1 = total_l1 / errs.size();

    printf("Average loss (L1): %.6f\n", avg_l1);
  }
}


int main(int argc, char **argv) {
  AnsiInit();

  CHECK(argc == 2) << "./evaluate-chess.exe modelfile\n";

  const string model_name = argv[1];

  std::unique_ptr<Network> net(
      Network::ReadFromFile(model_name));
  CHECK(net.get() != nullptr) << model_name;
  net->StructuralCheck();
  net->NaNCheck(model_name);

  Evaluate(*net);

  printf("OK\n");
  return 0;
}
