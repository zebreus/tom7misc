
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

  int across = 68;
  int down = 38;
  ImageRGBA out(28 * across, 28 * down);
  for (int y = 0; y < down; y++) {
    for (int x = 0; x < across; x++) {
      int idx = y * across + x;
      ImageA img = mnist.images[idx];

      for (int yy = 0; yy < img.Height(); yy++) {
        for (int xx = 0; xx < img.Width(); xx++) {
          float f = (float)img.GetPixel(xx, yy) / 255.0f;
          f += gauss.Next() * 0.25f;
          img.SetPixel(xx, yy, (uint8)std::clamp(
                           (int)std::round(f * 255.0f), 0, 255));
        }
      }

      out.CopyImage(x * 28, y * 28, img.GreyscaleRGBA());
      out.BlendText32(x * 28, y * 28, 0xFF000033,
                      StringPrintf("%c", mnist.labels[idx] + '0'));
    }
  }

  out.Save("mnist-test.png");

  printf("There are %d labels\n", (int)mnist.labels.size());
  return 0;
}
