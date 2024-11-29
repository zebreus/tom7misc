// This was a bug report for stb_truetype, which sean has
// fixed!

#include <stdio.h>
#include <unistd.h>

#include "base/logging.h"

#include "fonts/ttf.h"
#include "stb_truetype.h"
// #include "ttfops.h"

using namespace std;

using uint8 = uint8_t;
using int64 = int64_t;

int main(int argc, char **argv) {

  // TTF ttf{"laser-italic.ttf"};
  TTF ttf{"exedoreli.ttf"};
  const stbtt_fontinfo *info = ttf.FontInfo();

  [[maybe_unused]]
  float stb_scale = stbtt_ScaleForPixelHeight(info, 200.0f);

  int width2, height2, x2, y2;
  uint8 *bit2 = stbtt_GetCodepointBitmapSubpixel(info,
                                                 0.4972374737262726,
                                                 0.4986416995525360,
                                                 0.2391788959503174,
                                                 0.1752119064331055,
                                                 'd',
                                                 &width2, &height2,
                                                 &x2, &y2);

  stbtt_FreeBitmap(bit2, nullptr);

  printf("OK.\n");
  return 0;
}



