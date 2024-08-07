
#include "mov.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"

using Out = MOV::Out;

static constexpr int WIDTH = 1920;
static constexpr int HEIGHT = 1080;
static constexpr int FRAMES = 4;


static void MakeMovie() {
  std::unique_ptr<Out> out = MOV::OpenOut("test.mov", 1920, 1080,
                                          MOV::DURATION_60,
                                          MOV::Codec::PNG_MINIZ);

  for (int i = 0; i < FRAMES; i++) {
    ImageRGBA frame(WIDTH, HEIGHT);
    frame.Clear32(0x000033FF);
    for (int y = 0; y < HEIGHT; y++) {
      uint8_t a = 255 - std::clamp(y / (float)HEIGHT * 255.0f, 0.0f, 255.0f);
      for (int x = 24; x < 90; x++) {
        frame.SetPixel(x, y, 0xFF, 0x20, 0xFF, a);
      }
    }

    float f = i / (float)FRAMES;
    int x = WIDTH * (0.3 + f * 0.1);
    int y = HEIGHT * (0.2 + f * 0.4);
    frame.BlendFilledCircle32(x, y, WIDTH / 10, 0xAA3377FF);

    frame.BlendText2x32(100, 100, 0xFFFF00FF,
                        StringPrintf("Frame %d", i));

    out->AddFrame(frame);
  }

  MOV::CloseOut(out);
}

int main(int argc, char **argv) {
  ANSI::Init();
  MakeMovie();

  printf("OK\n");
  return 0;
}
