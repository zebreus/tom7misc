
#include <memory>

#include "network.h"
#include "clutil.h"

#include "timer.h"
#include "image.h"
#include "base/stringprintf.h"
#include "base/logging.h"

#include "eval-cifar10.h"

using namespace std;

static CL *cl = nullptr;

int main(int argc, char **argv) {
  CHECK(argc == 3) << "./evaluate-cifar10.exe modelfile wrong.png\n";
  cl = new CL;

  const string model_name = argv[1];
  const string wrong_file = argv[2];

  std::unique_ptr<Network> net(
      Network::ReadFromFile(model_name));
  CHECK(net.get() != nullptr) << model_name;
  net->StructuralCheck();
  net->NaNCheck(model_name);

  EvalCIFAR10 evaluator(cl);

  EvalCIFAR10::Result res = evaluator.Evaluate(net.get());
  EvalCIFAR10::TitleResult(&res);

  printf("Predicted %d in %.3fs\n",
         res.total, res.fwd_time);
  printf("%d/%d correct = %.3f%%\n",
         res.correct, res.total,
         (double)(res.correct * 100.0) / res.total);
  res.wrong.Save(wrong_file);

  static constexpr int CELL = 6;
  auto Label = [](int l) -> string {
      string s = CIFAR10::LabelString(l);
      if (s.size() > CELL - 1) s.resize(CELL - 1);
      return Util::Pad(CELL - 1, s);
    };
  printf("\n");
  printf("a \\ p "
         "corr%% ");
  for (int c = 0; c < CIFAR10::RADIX; c++) {
    printf(" %s", Label(c).c_str());
  }
  printf("\n");

  for (int r = 0; r < CIFAR10::RADIX; r++) {
    // This is always expected to be res.total / radix.
    int tot = 0;
    for (int c = 0; c < CIFAR10::RADIX; c++)
      tot += res.conf[r][c];
    float rcorrect = (res.conf[r][r] * 100.0f) / tot;
    string p = Util::Pad(CELL - 1, StringPrintf("%.1f", rcorrect));
    printf("%s %s", Label(r).c_str(), p.c_str());
    for (int c = 0; c < CIFAR10::RADIX; c++) {
      string n = Util::Pad(CELL - 1, StringPrintf("%d", res.conf[r][c]));
      printf(" %s", n.c_str());
    }
    printf("\n");
  }

  return 0;
}
