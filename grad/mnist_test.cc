
#include "mnist.h"

#include <cstdio>
#include <cstdint>
#include <string>

#include "image.h"
#include "base/stringprintf.h"

int main(int argc, char **argv) {
  MNIST mnist("mnist/train");

  CHECK(mnist.width == 28 && mnist.height == 28);

  int across = 68;
  int down = 38;
  ImageRGBA out(28 * across, 28 * down);
  for (int y = 0; y < down; y++) {
    for (int x = 0; x < across; x++) {
      int idx = y * across + x;
      const ImageA &img = mnist.images[idx];
      out.CopyImage(x * 28, y * 28, img.GreyscaleRGBA());
      out.BlendText32(x * 28, y * 28, 0xFF000033,
                      StringPrintf("%c", mnist.labels[idx] + '0'));
    }
  }

  out.Save("mnist-test.png");

  printf("There are %d labels\n", (int)mnist.labels.size());
  return 0;
}
