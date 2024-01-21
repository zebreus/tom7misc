
#include <string>
#include <string_view>

#include "ast.h"
#include "frontend.h"

#include "util.h"
#include "base/logging.h"
#include "ansi.h"

static int Bovex(const std::vector<std::string> &args) {
  Frontend frontend;
  // Parse command-line arguments.

  int verbose = 0;

  std::vector<std::string> leftover;
  for (int i = 0; i < (int)args.size(); i++) {
    std::string_view arg(args[i]);
    if (Util::TryStripPrefix("-v", &arg)) {
      verbose++;
      printf("Verbosity: %d\n", verbose);
    } else if (Util::TryStripPrefix("-I", &arg)) {
      // Then this is -Istdlib
      if (!arg.empty()) {
        frontend.AddIncludePath((std::string)arg);
      } else {
        CHECK(i + 1 < (int)args.size()) << "Trailing -I argument";
        i++;
        frontend.AddIncludePath(args[i]);
      }
    } else {
      leftover.push_back((std::string)arg);
    }
  }

  if (verbose > 0) {
    printf(AWHITE("Remaining args:"));
    for (const std::string &arg : leftover) {
      printf(" " AGREY("[") "%s" AGREY("]"), arg.c_str());
    }
    printf("\n");
  }

  frontend.SetVerbose(verbose);

  CHECK(leftover.size() == 1) << "Expected exactly one .bovex file "
    "on the command-line.";

  const Exp *e = frontend.RunFrontend(leftover[0]);

  CHECK(e != nullptr);

  return 0;
}


int main(int argc, char **argv) {
  ANSI::Init();
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.emplace_back(argv[i]);
  }

  return Bovex(args);
}
