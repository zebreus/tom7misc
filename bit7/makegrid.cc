
#include <cstdint>

#include "image.h"

#include "base/stringprintf.h"

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

constexpr int CHARS_ACROSS = 16;
constexpr int CHARS_DOWN = 8;

int main(int argc, char **argv) {
  // XXX from command-line.
  const int CHAR_WIDTH = 18;
  const int CHAR_HEIGHT = 16;

  ImageRGBA grid{CHAR_WIDTH * CHARS_ACROSS, CHAR_HEIGHT * CHARS_DOWN};
  grid.Clear(0, 0, 0, 0xFF);

  // number of pixels on the bottom to shade as "descent"
  static constexpr int DESCENT = 4;
  static_assert (DESCENT >= 0 && DESCENT <= CHAR_HEIGHT);

  static constexpr int SPACING = 5;
  static_assert (SPACING >= 0 && SPACING <= CHAR_WIDTH);

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
	  bool darker = y >= (CHAR_HEIGHT - DESCENT) || x >= (CHAR_WIDTH - SPACING);
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
  
  // XXX filename including dimensions, or from command line?
  grid.Save(StringPrintf("grid%dx%d.png", CHAR_WIDTH, CHAR_HEIGHT));

  for (int y = 0; y < CHARS_DOWN; y++) {
    for (int x = 0; x < CHARS_ACROSS; x++) {
      int c = y * CHARS_ACROSS + x;
      printf("%c ", (c < 32 || c >= 127) ? '_' : c);
    }
    printf("\n");
  }
  
  return 0;
}
