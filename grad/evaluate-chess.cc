
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "network.h"
#include "base/logging.h"
#include "ansi.h"

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
