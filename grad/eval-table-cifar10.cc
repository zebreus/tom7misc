
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
  vector<tuple<string, string, bool>> experiments = {
    {"sigmoid", "cifar10-sigmoid", false},
    {"tanh", "cifar10-tanh", false},
    {"leaky-relu", "cifar10-leaky", false},
    {"plus64", "cifar10-plus64", false},
    {"grad1", "cifar10-grad1", false},
    {"identity", "cifar10-identity", false},
    {"downshift2", "cifar10-downshift2", false},

    {"plus64", "cifar10-plus64", true},
    {"grad1", "cifar10-grad1", true},
    {"identity", "cifar10-identity", true},
  };

  CHECK(cl != nullptr);
  EvalCIFAR10 eval(cl);

  string table =
    StringPrintf(
        "\\begin{tabular}{rcr}\n"
        "{\\bf transfer function} & {\\bf flat} & {\\bf accuracy} \\\\\n"
        "\\hline \n");
  for (const auto &[name, dir, flatten] : experiments) {
    string model_file = Util::dirplus(dir, "classify.val");
    std::unique_ptr<Network> net(
        Network::ReadFromFile(model_file));
    CHECK(net.get() != nullptr) << model_file;
    net->StructuralCheck();
    net->NaNCheck(model_file);

    if (flatten) {
      printf(APURPLE("flatten") " " ACYAN("%s") "...\n",
             name.c_str());
      Network flat = Network::Flatten(*net);
      *net = flat;
    }

    EvalCIFAR10::Result result = eval.Evaluate(net.get());
    double pct = (result.correct * 100.0) / result.total;
    StringAppendF(&table, "%s & %s & %.2f\\%% \\\\\n",
                  name.c_str(), flatten ? "$\\times$" : " ", pct);
    printf(ACYAN("%s") "%s: " ABLUE("%.3f") "%%\n",
           name.c_str(),
           flatten ? " " APURPLE("(flat)") : "",
           pct);
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
