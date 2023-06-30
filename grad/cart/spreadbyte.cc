
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

// Makes a hexadecimal table (full byte in each tile) from
// 4x8 font in the first row.
int main(int argc, char **argv) {

  CHECK(argc == 3) << "./spreadbyte.exe in.png out.png\n";

  std::unique_ptr<ImageRGBA> img(ImageRGBA::Load(argv[1]));
  CHECK(img->Width() == 256);
  CHECK(img->Height() == 128);

  for (int tile = 0; tile < 256; tile++) {
    int xx = tile % 16;
    int yy = tile / 16;

    // Fill its pixels using the first row.
    if (yy == 0)
      continue;

    // Left nybble.
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 4; x++) {
        const auto [r, g, b, a] = img->GetPixel(128 + yy * 8 + x + 4, y);
        int color = GetColor(r, g, b, a);
        uint8 v = "\x00\x55\xAA\xff"[color ? 1 : 0];
        img->SetPixel(128 + xx * 8 + x, yy * 8 + y,
                      v, v, v, 0xFF);
      }
    }
    // Right.
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 4; x++) {
        const auto [r, g, b, a] = img->GetPixel(128 + xx * 8 + x + 4, y);
        int color = GetColor(r, g, b, a);
        uint8 v = "\x00\x55\xAA\xff"[color ? 3 : 0];
        img->SetPixel(128 + xx * 8 + x + 4, yy * 8 + y,
                      v, v, v, 0xFF);
      }
    }
  }

  img->Save(argv[2]);

  return 0;
}
