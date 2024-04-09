
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>

#include "util.h"
#include "image.h"
#include "color-util.h"

static bool INCLUDE_TESTS = false;
static constexpr bool BREAK_NEWLINE = true;
static constexpr int GAP = 0;

int main(int argc, char **argv) {

  std::vector<std::string> source;
  for (const std::string &ent : Util::ListFiles(".")) {
    if (Util::isdir(ent)) continue;
    if (Util::MatchesWildcard("*.cc", ent) ||
        Util::MatchesWildcard("*.h", ent) ||
        (ent == "makefile") ||
        (ent == "stdlib.bovex") ||
        (ent == "layout.bovex") ||
        (ent == "cite.bovex")) {

      if (INCLUDE_TESTS || !Util::MatchesWildcard("*_test.cc", ent)) {
        source.push_back(ent);
      }
    }
  }

  source.push_back("../cc-lib/parser-combinators.h");
  source.push_back("../cc-lib/pdf.h");
  source.push_back("../cc-lib/pdf.cc");

  std::sort(source.begin(), source.end());

  ImageRGBA img(1920, 1080);
  ImageRGBA img_self(1920, 1080);
  img.Clear32(0x000000FF);
  img_self.Clear32(0x000000FF);

  int xcol = 0;
  int xpos = 0, ypos = 0;
  for (const std::string &filename : source) {
    bool self = filename == "makecodeimage.cc";
    printf("[%s]%s\n", filename.c_str(),
           self ? " *****" : "");
    std::vector<uint8_t> bytes = Util::ReadFileBytes(filename);
    for (uint8_t byte : bytes) {
      if (BREAK_NEWLINE && byte == '\n') {
        xpos = 81;

      } else {
        uint32_t color = ColorUtil::Pack32(byte, byte, byte, 0xFF);

        img.SetPixel32(xcol * (80 + GAP) + xpos, ypos, color);
        if (self) color |= 0xFF000000;
        img_self.SetPixel32(xcol * (80 + GAP) + xpos, ypos, color);

        xpos++;
      }

      if (xpos >= 80) {
        ypos++;
        xpos = 0;
        if (ypos >= 1080) {
          xcol++;
          ypos = 0;
          xpos = 0;
        }
      }

    }
  }

  int w = xcol * (80 + GAP) + xpos;
  printf("final pos %d,%d (%.3f%%)\n", w, ypos, 192000.0 / w);

  img.Save("code.png");
  img_self.Save("code-self.png");

  return 0;
}
