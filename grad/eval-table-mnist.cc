
#include <vector>
#include <string>
#include <tuple>

#include "eval-mnist.h"
#include "util.h"
#include "ansi.h"
#include "network.h"
#include "clutil.h"

using namespace std;

static CL *cl = nullptr;


static void MakeEvalTableMNIST(const string &outfile) {
  vector<tuple<string, string>> experiments = {
    {"sigmoid", "mnist-sigmoid"},
    {"tanh", "mnist-tanh"},
    {"leaky-relu", "mnist-leaky"},
    {"plus64", "mnist-plus64"},
    {"grad1", "mnist-grad1"},
    {"identity", "mnist-identity"},
    {"downshift2", "mnist-downshift2"},
  };

  CHECK(cl != nullptr);
  EvalMNIST eval(cl);

  string table =
    StringPrintf(
        "\\begin{tabular}{rr}\n"
        "{\\bf transfer function} & {\\bf accuracy} \\\\\n"
        "\\hline \n");
  for (const auto &[name, dir] : experiments) {
    string model_file = Util::dirplus(dir, "grad.val");
    std::unique_ptr<Network> net(
        Network::ReadFromFile(model_file));
    CHECK(net.get() != nullptr) << model_file;
    net->StructuralCheck();
    net->NaNCheck(model_file);

    EvalMNIST::Result result = eval.Evaluate(net.get());
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

  CHECK(argc == 2) << "./eval-table-mnist.exe outfile.tex";
  MakeEvalTableMNIST(argv[1]);

  return 0;
}
