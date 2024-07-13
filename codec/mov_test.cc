
#include "mov.h"

#include "ansi.h"
#include "base/logging.h"
#include "image.h"

using Out = MOV::Out;

static constexpr int WIDTH = 1920;
static constexpr int HEIGHT = 1080;
static constexpr int FRAMES = 4;


static void MakeMovie() {
  std::unique_ptr<Out> out = MOV::OpenOut("test.mov", 1920, 1080);

  for (int i = 0; i < FRAMES; i++) {
    ImageRGBA frame(WIDTH, HEIGHT);
    frame.Clear32(0x000033FF);

    float f = i / (float)FRAMES;
    int x = WIDTH * (0.3 + f * 0.1);
    int y = HEIGHT * (0.2 + f * 0.4);
    frame.BlendFilledCircle32(x, y, WIDTH / 10, 0xAA3377FF);
    out->AddFrame(frame);
  }

  MOV::CloseOut(out);
}

int main(int argc, char **argv) {
  MakeMovie();

  printf("OK\n");
  return 0;
}
