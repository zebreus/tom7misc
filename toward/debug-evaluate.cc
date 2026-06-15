
#include <string>
#include <string_view>

#include "eval.h"
#include "image.h"
#include "letters.h"
#include "ansi.h"
#include "base/print.h"
#include "util.h"

static void DebugEvaluate(std::string_view font_filename) {
  auto letters = Letters::LoadFont(font_filename);
  CHECK(letters != nullptr) << "Failed to load font: " << font_filename;

  std::string base_name{Util::FileBaseOf(Util::FileOf(font_filename))};

  for (char c = 'A'; c <= 'Z'; c++) {
    auto it = letters->letter.find(c);
    if (it != letters->letter.end()) {
      ImageRGBA img = Eval::DebugStability(it->second);
      std::string out_name = base_name + "-" + c + ".png";
      img.Save(out_name);
    }
  }

}

int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 2) << "Pass a font filename on the command line.";

  DebugEvaluate(argv[1]);

  Print("OK\n");
  return 0;
}
