
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
  vector<tuple<string, string>> experiments = {
    {"sigmoid", "chess-sigmoid"},
    {"tanh", "chess-tanh"},
    {"leaky-relu", "chess-leaky"},
    {"plus64", "chess-plus64"},
    {"grad1", "chess-grad1"},
    {"identity", "chess-identity"},
    {"downshift2", "chess-downshift2"},
  };

  EvalChess eval;

  string table =
    StringPrintf(
        "\\begin{tabular}{rrr}\n"
        "{\\bf transfer function} & {\\bf loss} & {\\bf accuracy} \\\\\n"
        "\\hline \n");
  for (const auto &[name, dir] : experiments) {
    string model_file = Util::dirplus(dir, "chess.val");
    std::unique_ptr<Network> net(
        Network::ReadFromFile(model_file));
    CHECK(net.get() != nullptr) << model_file;
    net->StructuralCheck();
    net->NaNCheck(model_file);

    const auto [loss, acc] = eval.Evaluate(*net);
    double pct = acc * 100.0;
    StringAppendF(&table, "%s & %.3f & %.3f\\%% \\\\\n",
                  name.c_str(), loss, pct);
    printf(ACYAN("%s") ": " APURPLE("%.3f") " " ABLUE("%.3f") "%%\n",
           name.c_str(), loss, pct);
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
