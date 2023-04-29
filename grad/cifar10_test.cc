
#include "cifar10.h"

#include <cstdio>
#include <cstdint>
#include <string>

#include "image.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"

static constexpr float NOISE_SCALE = 0.025f;

static constexpr bool ADD_OFFSET = false;
static constexpr bool ADD_GAUSS = false;
static constexpr bool SHOW_LABEL = false;

int main(int argc, char **argv) {
  CIFAR10 cifar10("cifar10", false);
  CHECK(CIFAR10::WIDTH == CIFAR10::HEIGHT);
  printf("%d labels, %d images.\n",
         (int)cifar10.labels.size(),
         (int)cifar10.images.size());

  ArcFour rc("test");
  RandomGaussian gauss(&rc);

  int PAD = 1;
  int across = 68;
  int down = 38;

  const int SQUARE = CIFAR10::WIDTH + PAD;
  ImageRGBA out(SQUARE * across, SQUARE * down);

  out.Clear32(0x000055FF);
  for (int y = 0; y < down; y++) {
    for (int x = 0; x < across; x++) {
      int idx = y * across + x;
      CHECK(idx < cifar10.images.size());
      const ImageRGBA &img = cifar10.images[idx];
      ImageRGBA img_out(32, 32);

      int dx = 0, dy = 0;
      if (ADD_OFFSET) {
        dx = RandTo(&rc, 5) - 2;
        dy = RandTo(&rc, 5) - 2;
      }
      for (int yy = 0; yy < img.Height(); yy++) {
        for (int xx = 0; xx < img.Width(); xx++) {
          auto [r, g, b, a_] = img.GetPixel(xx + dx, yy + dy);
          float fr = (r / 255.0f);
          float fg = (g / 255.0f);
          float fb = (b / 255.0f);

          if (ADD_GAUSS) {
            fr += gauss.Next() * NOISE_SCALE;
            fg += gauss.Next() * NOISE_SCALE;
            fb += gauss.Next() * NOISE_SCALE;
          }

          uint8_t ur = std::clamp((int)std::round(fr * 255.0f), 0, 255);
          uint8_t ug = std::clamp((int)std::round(fg * 255.0f), 0, 255);
          uint8_t ub = std::clamp((int)std::round(fb * 255.0f), 0, 255);
          img_out.SetPixel(xx, yy, ur, ug, ub, 0xFF);
        }
      }

      out.CopyImage(x * SQUARE, y * SQUARE, img_out);
      if (SHOW_LABEL) {
        out.BlendText32(x * SQUARE, y * SQUARE, 0xFF000055,
                        StringPrintf("%c", cifar10.labels[idx] + '0'));
      }
    }
  }

  out.ScaleBy(3).Save("cifar10-test.png");

  printf("There are %d labels\n", (int)cifar10.labels.size());
  return 0;
}
