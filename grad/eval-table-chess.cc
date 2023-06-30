
#include <vector>
#include <string>
#include <tuple>

#include "eval-chess.h"
#include "util.h"
#include "ansi.h"
#include "network.h"
#include "clutil.h"

using namespace std;

static void MakeEvalTableChess(const string &outfile) {
  vector<tuple<string, string, bool>> experiments = {
    {"logistic", "chess-sigmoid", false},
    {"tanh", "chess-tanh", false},
    {"leaky-relu", "chess-leaky", false},
    {"plus64", "chess-plus64", false},
    {"grad1", "chess-grad1", false},
    {"identity", "chess-identity", false},
    {"downshift2", "chess-downshift2", false},

    {"plus64", "chess-plus64", true},
    {"grad1", "chess-grad1", true},
    {"identity", "chess-identity", true},
  };

  EvalChess eval;

  string table =
    StringPrintf(
        "\\begin{tabular}{rcrr}\n"
        "{\\bf transfer function} & {\\bf flat} & "
        "{\\bf loss} & {\\bf accuracy} \\\\\n"
        "\\hline \n");
  for (const auto &[name, dir, flatten] : experiments) {
    string model_file = Util::dirplus(dir, "chess.val");
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

    const auto [loss, acc] = eval.Evaluate(*net);
    double pct = acc * 100.0;
    StringAppendF(&table, "%s & %s & %.3f & %.3f\\%% \\\\\n",
                  name.c_str(), flatten ? "$\\times$" : " ", loss, pct);
    printf(ACYAN("%s") "%s: " APURPLE("%.3f") " " ABLUE("%.3f") "%%\n",
           name.c_str(),
           flatten ? " " APURPLE("(flat)") : "",
           loss, pct);
  }
  StringAppendF(&table, "\\end{tabular}\n");

  Util::WriteFile(outfile, table);
  printf("Wrote " AGREEN ("%s") "\n", outfile.c_str());
}

int main(int argc, char **argv) {
  AnsiInit();

  CHECK(argc == 2) << "./eval-table-chess.exe outfile.tex";
  MakeEvalTableChess(argv[1]);

  return 0;
}
