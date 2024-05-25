
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

#include "util.h"
#include "image.h"
#include "color-util.h"
#include "base/logging.h"

static bool INCLUDE_TESTS = true;
static constexpr bool BREAK_NEWLINE = true;
static constexpr int GAP = 2;

static constexpr int MARGIN_TOP = 300;
static constexpr int MARGIN_BOTTOM = 300;
static constexpr int MARGIN_LEFT = 800;
static constexpr int MARGIN_RIGHT = 800;

int main(int argc, char **argv) {

  CHECK(argc == 5) << "makecodeimage.exe 1920 1080 out.png out-self.png\n";

  int width = atoi(argv[1]);
  int height = atoi(argv[2]);
  std::string filename = argv[3];
  std::string filename_self = argv[4];
  CHECK(width > 0 && height > 0 && !filename.empty() &&
        !filename_self.empty());

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

  source.push_back("chess.bovex");
  source.push_back("video/shared.bovex");

  source.push_back("../cc-lib/parser-combinators.h");
  if (INCLUDE_TESTS) {
    source.push_back("../cc-lib/parser-combinators_test.cc");
  }
  source.push_back("../cc-lib/pdf.h");
  source.push_back("../cc-lib/pdf.cc");
  if (INCLUDE_TESTS) {
    source.push_back("../cc-lib/pdf_test.cc");
  }

  std::sort(source.begin(), source.end());

  ImageRGBA img(width, height);
  ImageRGBA img_self(width, height);
  img.Clear32(0x000000FF);
  img_self.Clear32(0x000000FF);

  const int CONTENT_HEIGHT = height - MARGIN_TOP - MARGIN_BOTTOM;

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

        img.SetPixel32(MARGIN_LEFT + xcol * (80 + GAP) + xpos,
                       MARGIN_TOP + ypos,
                       color);
        if (self) color |= 0xFF000000;
        img_self.SetPixel32(MARGIN_LEFT + xcol * (80 + GAP) + xpos,
                            MARGIN_TOP + ypos,
                            color);

        xpos++;
      }

      if (xpos >= 80) {
        ypos++;
        xpos = 0;
        if (ypos >= CONTENT_HEIGHT) {
          xcol++;
          ypos = 0;
          xpos = 0;
        }
      }

    }
  }

  int w = xcol * (80 + GAP) + xpos;
  printf("final pos %d,%d (%.3f%%)\n", w, ypos, (width * 100.0) / w);

  img.Save(filename);
  img_self.Save(filename_self);

  return 0;
}
