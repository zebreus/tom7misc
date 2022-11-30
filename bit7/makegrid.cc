
#include <cstdint>
#include <memory>
#include <string>

#include "image.h"

#include "base/stringprintf.h"
#include "config.h"

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

int main(int argc, char **argv) {
  CHECK(argc == 2) << "./makegrid.exe config.cfg\n";

  const Config config = Config::ParseConfig(argv[1]);


  // TODO: Make this configurable from command-line
  // (probably we could take a second config file).
  // Use this image as starting char data, if non-empty.
  const string in_file = ""; // "dfx-snooty.png";
  const int IN_CHAR_WIDTH = 11;
  const int IN_CHAR_HEIGHT = 10;

  // XXX from command-line.
  const int CHAR_WIDTH = config.charbox_width;
  const int CHAR_HEIGHT = config.charbox_height;

  const int CHARS_ACROSS = config.chars_across;
  const int CHARS_DOWN = config.chars_down;

  CHECK(config.fixed_width) << "TODO: I should add black bars or at "
    "least make space for them if the config is not fixed-width.";

  // number of pixels on the bottom to shade as "descent"
  const int DESCENT = config.descent;
  CHECK(DESCENT >= 0 && DESCENT <= CHAR_HEIGHT);

  // number of pixels on the right to shade
  const int SPACING = config.spacing;
  CHECK(SPACING >= 0 && SPACING <= CHAR_WIDTH);

  std::unique_ptr<ImageRGBA> infont;
  if (!in_file.empty()) {
    infont.reset(ImageRGBA::Load(in_file));
    CHECK(infont.get() != nullptr) << in_file;
    CHECK(infont->Width() == IN_CHAR_WIDTH * CHARS_ACROSS &&
          infont->Height() == IN_CHAR_HEIGHT * CHARS_DOWN) <<
      "Expected infont of size " << (IN_CHAR_WIDTH * CHARS_ACROSS) <<
      "x" << (IN_CHAR_HEIGHT * CHARS_DOWN) << " but got " <<
      infont->Width() << "x" << infont->Height();
  }

  ImageRGBA grid{CHAR_WIDTH * CHARS_ACROSS, CHAR_HEIGHT * CHARS_DOWN};
  grid.Clear(0, 0, 0, 0xFF);

  // XXX different colors for descent/edge?
  static constexpr uint32 ODD_COLOR = 0x000033FF;
  static constexpr uint32 ODD_DARKER = 0x000027FF;
  static constexpr uint32 EVEN_COLOR = 0x333300FF;
  static constexpr uint32 EVEN_DARKER = 0x272700FF;

  for (int cy = 0; cy < CHARS_DOWN; cy++) {
    for (int cx = 0; cx < CHARS_ACROSS; cx++) {
      const bool odd = (cx + cy) & 1;

      for (int y = 0; y < CHAR_HEIGHT; y++) {
        for (int x = 0; x < CHAR_WIDTH; x++) {
          bool darker = y >= (CHAR_HEIGHT - DESCENT) ||
            x >= (CHAR_WIDTH - SPACING);
          uint32 c = odd ?
            (darker ? ODD_DARKER : ODD_COLOR) :
            (darker ? EVEN_DARKER : EVEN_COLOR);
          int xx = cx * CHAR_WIDTH + x;
          int yy = cy * CHAR_HEIGHT + y;
          grid.SetPixel32(xx, yy, c);
        }
      }
    }
  }

  if (infont.get() != nullptr) {
    // XXX interpret the bitmap data a la some common code factored
    // out of makesfd.exe. For example, adding height here doesn't
    // extend the width marker.
    int cwidth = std::min(IN_CHAR_WIDTH, CHAR_WIDTH);
    int cheight = std::min(IN_CHAR_HEIGHT, CHAR_HEIGHT);

    for (int y = 0; y < CHARS_DOWN; y++) {
      for (int x = 0; x < CHARS_ACROSS; x++) {
        int destx = x * CHAR_WIDTH;
        int desty = y * CHAR_HEIGHT;
        int srcx = x * IN_CHAR_WIDTH;
        int srcy = y * IN_CHAR_HEIGHT;
        grid.CopyImageRect(destx, desty, *infont,
                           srcx, srcy, cwidth, cheight);
      }
    }
  }

  // XXX filename from config if present?
  grid.Save(StringPrintf("grid%dx%d.png", CHAR_WIDTH, CHAR_HEIGHT));

  for (int y = 0; y < CHARS_DOWN; y++) {
    for (int x = 0; x < CHARS_ACROSS; x++) {
      int c = y * CHARS_ACROSS + x;
      printf("%c", (c < 32 || c >= 255) ? '_' : c);
    }
    printf("\n");
  }

  return 0;
}

