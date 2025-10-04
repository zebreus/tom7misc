// Outputs all the unicode codepoints in a font, as UTF-8 strings.

#include <algorithm>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "font-image.h"
#include "utf8.h"

int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 2) << "./getcodepoints.exe font.cfg\n";

  Config config = Config::ParseConfig(argv[1]);
  FontImage font(config);

  std::vector<int> codepoints;
  for (const auto &[codepoint, _] : font.GetUnicode()) {
    codepoints.push_back(codepoint);
  }

  std::sort(codepoints.begin(), codepoints.end());

  for (int codepoint : codepoints) {
    Print("{}", UTF8::Encode(codepoint));
  }
  Print("\n");

  return 0;
}
