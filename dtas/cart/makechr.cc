
#include <cstdint>
#include <memory>
#include <vector>

#include "image.h"
#include "util.h"

using namespace std;
using uint8 = uint8_t;

static uint8 GetColor(uint8 r, uint8 g, uint8 b, uint8 a) {
  int v = (r + g + (int)b) / 3;
  if (v < 0x40) return 0x00;
  if (v < 0x88) return 0x01;
  if (v < 0xCC) return 0x02;
  return 0x03;
}

int main(int argc, char **argv) {

  CHECK(argc == 3) << "./makechr.exe in.png out.chr\n";

  std::unique_ptr<ImageRGBA> img(ImageRGBA::Load(argv[1]));
  CHECK(img->Width() == 256);
  CHECK(img->Height() == 128);

  std::vector<uint8> chr(8192, 0x00);


  for (int bank = 0; bank < 2; bank++) {
    for (int tile = 0; tile < 256; tile++) {
      int xx = bank * 128 + (tile % 16) * 8;
      int yy = (tile / 16) * 8;

      const int idx = bank * 0x1000 + tile * 16;
      for (int y = 0; y < 8; y++) {
        uint8 hi = 0; // chr[idx + y];
        uint8 lo = 0; // chr[idx + y + 8];
        for (int x = 0; x < 8; x++) {
          const auto [r, g, b, a] = img->GetPixel(xx + x, yy + y);
          int color = GetColor(r, g, b, a);

          uint8 hi_bit = color >> 1;
          uint8 lo_bit = color & 1;

          hi |= (hi_bit << (7 - x));
          lo |= (lo_bit << (7 - x));
        }
        // XXX I swapped the order of lo/hi here because I wasn't getting
        // the colors I expected. Not sure if the bug was here or elsewhere,
        // though? 4 Jul 2023
        chr[idx + y] = lo;
        chr[idx + y + 8] = hi;
      }
    }
  }

  Util::WriteFileBytes(argv[2], chr);

  return 0;
}
