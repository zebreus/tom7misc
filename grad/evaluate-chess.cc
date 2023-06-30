
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
#include "eval-chess.h"

using namespace std;

using int64 = int64_t;
using uint64 = uint64_t;
using uint8 = uint8_t;

static void RunEval(const Network &net) {
  EvalChess eval;
  const auto [l1, sign] = eval.Evaluate(net);
  printf("%.3f, %.3f\n", l1, sign);
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

  RunEval(*net);

  printf("OK\n");
  return 0;
}
