
#include <memory>

#include "network.h"
#include "clutil.h"

#include "timer.h"
#include "image.h"
#include "base/stringprintf.h"

#include "eval.h"

using namespace std;

static constexpr const char *MODEL_NAME = "grad.val";

static CL *cl = nullptr;

int main(int argc, char **argv) {
  cl = new CL;

  std::unique_ptr<Network> net(
      Network::ReadFromFile(MODEL_NAME));
  CHECK(net.get() != nullptr);
  net->StructuralCheck();
  net->NaNCheck(MODEL_NAME);

  Evaluator evaluator(cl);

  Evaluator::Result res = evaluator.Evaluate(net.get());

  printf("Predicted %d in %.3fs\n",
         res.total, res.fwd_time);
  printf("%d/%d correct = %.3f%%\n",
         res.correct, res.total,
         (double)(res.correct * 100.0) / res.total);
  res.wrong.Save("wrong.png");


  return 0;
}
