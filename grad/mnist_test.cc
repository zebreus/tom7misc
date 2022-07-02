
#include "mnist.h"

#include <cstdio>
#include <cstdint>
#include <string>

#include "image.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"

int main(int argc, char **argv) {
  MNIST mnist("mnist/train");


  CHECK(mnist.width == 28 && mnist.height == 28);

  ArcFour rc("test");
  RandomGaussian gauss(&rc);

  int PAD = 1;
  int across = 68;
  int down = 38;

  const int SQUARE = 28 + PAD;
  ImageRGBA out(SQUARE * across, SQUARE * down);
  out.Clear32(0x000055FF);
  for (int y = 0; y < down; y++) {
    for (int x = 0; x < across; x++) {
      int idx = y * across + x;
      const ImageA &img = mnist.images[idx];
      ImageA img_out(28, 28);

      int dx = RandTo(&rc, 5) - 2;
      int dy = RandTo(&rc, 5) - 2;
      for (int yy = 0; yy < img.Height(); yy++) {
        for (int xx = 0; xx < img.Width(); xx++) {
          float f = (float)img.GetPixel(xx + dx, yy + dy) / 255.0f;
          f += gauss.Next() * 0.25f;
          img_out.SetPixel(xx, yy, (uint8)std::clamp(
                               (int)std::round(f * 255.0f), 0, 255));
        }
      }

      out.CopyImage(x * SQUARE, y * SQUARE, img_out.GreyscaleRGBA());
      out.BlendText32(x * SQUARE, y * SQUARE, 0xFF000055,
                      StringPrintf("%c", mnist.labels[idx] + '0'));
    }
  }

  out.Save("mnist-test.png");

  printf("There are %d labels\n", (int)mnist.labels.size());
  return 0;
}
