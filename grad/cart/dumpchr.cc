
#include <cstdint>

#include "image.h"
#include "util.h"

using namespace std;
using uint8 = uint8_t;

int main(int argc, char **argv) {

  CHECK(argc == 3) << "./dumpchr.exe in.chr out.png\n";

  std::vector<uint8> chr = Util::ReadFileBytes(argv[1]);

  ImageRGBA img(256, 128);
  img.Clear32(0xFF0000FF);
  for (int bank = 0; bank < 2; bank++) {
    for (int tile = 0; tile < 256; tile++) {
      int xx = bank * 128 + (tile % 16) * 8;
      int yy = (tile / 16) * 8;

      const int idx = bank * 0x1000 + tile * 16;
      for (int y = 0; y < 8; y++) {
        uint8 hi = chr[idx + y];
        uint8 lo = chr[idx + y + 8];
        for (int x = 0; x < 8; x++) {
          int color =
            (((hi >> (7 - x)) & 1) << 1) |
            ((lo >> (7 - x)) & 1);

          uint8 v = "\x00\x55\xAA\xff"[color];
          img.SetPixel(xx + x, yy + y, v, v, v, 0xFF);
        }
      }
    }
  }

  img.Save(argv[2]);

  return 0;
}
