
#include <vector>
#include <string>
#include <tuple>

#include "eval-cifar10.h"
#include "util.h"
#include "ansi.h"
#include "network.h"
#include "clutil.h"

using namespace std;

static CL *cl = nullptr;


static void MakeEvalTableCIFAR10(const string &outfile) {
  vector<tuple<string, string>> experiments = {
    {"sigmoid", "cifar10-sigmoid"},
    {"tanh", "cifar10-tanh"},
    {"leaky-relu", "cifar10-leaky"},
    {"plus64", "cifar10-plus64"},
    {"grad1", "cifar10-grad1"},
    {"identity", "cifar10-identity"},
    {"downshift2", "cifar10-downshift2"},
  };

  CHECK(cl != nullptr);
  EvalCIFAR10 eval(cl);

  string table =
    StringPrintf(
        "\\begin{tabular}{rr}\n"
        "{\\bf transfer function} & {\\bf accuracy} \\\\\n"
        "\\hline \n");
  for (const auto &[name, dir] : experiments) {
    string model_file = Util::dirplus(dir, "classify.val");
    std::unique_ptr<Network> net(
        Network::ReadFromFile(model_file));
    CHECK(net.get() != nullptr) << model_file;
    net->StructuralCheck();
    net->NaNCheck(model_file);

    EvalCIFAR10::Result result = eval.Evaluate(net.get());
    double pct = (result.correct * 100.0) / result.total;
    StringAppendF(&table, "%s & %.2f\\%% \\\\\n",
                  name.c_str(), pct);
    printf(ACYAN("%s") ": " ABLUE("%.3f") "%%\n",
           name.c_str(), pct);
  }
  StringAppendF(&table, "\\end{tabular}\n");

  Util::WriteFile(outfile, table);
  printf("Wrote " AGREEN ("%s") "\n", outfile.c_str());
}

int main(int argc, char **argv) {
  AnsiInit();
  cl = new CL;

  CHECK(argc == 2) << "./eval-table-cifar10.exe outfile.tex";
  MakeEvalTableCIFAR10(argv[1]);

  return 0;
}
