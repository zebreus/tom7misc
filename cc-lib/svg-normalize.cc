
#include "svg.h"

#include <string_view>

#include "base/logging.h"
#include "base/print.h"
#include "util.h"

static void Normalize(std::string_view infile, std::string_view outfile) {
  SVG::Doc doc = SVG::ParseOrDie(Util::ReadFile(infile));
  Util::WriteFile(outfile, SVG::ToSVG(doc));
  Print("Wrote {}\n", outfile);
}

int main(int argc, char **argv) {
  CHECK(argc == 3) << "./svg-normalize.exe in.svg out.svg\n";

  Normalize(argv[1], argv[2]);

  return 0;
}
