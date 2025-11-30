
#include <memory>
#include <string>

#include "base/logging.h"
#include "base/print.h"
#include "image.h"
#include "opencl/clutil.h"

#include "eval-mnist.h"
#include "network.h"

using namespace std;

static CL *cl = nullptr;

int main(int argc, char **argv) {
  CHECK(argc == 3) << "./evaluate-mnist.exe modelfile wrong.png\n";
  cl = new CL;

  const string model_name = argv[1];
  const string wrong_file = argv[2];

  std::unique_ptr<Network> net(
      Network::ReadFromFile(model_name));
  CHECK(net.get() != nullptr) << model_name;
  net->StructuralCheck();
  net->NaNCheck(model_name);

  EvalMNIST evaluator(cl);

  EvalMNIST::Result res = evaluator.Evaluate(net.get());

  Print("Predicted {} in {:.3f}s\n",
        res.total, res.fwd_time);
  Print("{}/{} correct = {:.3f}%\n",
        res.correct, res.total,
        (double)(res.correct * 100.0) / res.total);
  res.wrong.Save(wrong_file);

  return 0;
}
