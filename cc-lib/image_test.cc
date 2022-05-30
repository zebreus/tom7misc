
#include "image.h"

#include "base/stringprintf.h"
#include "base/logging.h"
#include "arcfour.h"
#include "randutil.h"

static void TestBilinearResize() {
  ImageA in(20, 20);
  in.Clear(0);
  for (int i = 0; i < 16; i++) {
    in.SetPixel(i, i / 2, i * 15);
  }

  in.BlendText(1, 9, 0xCC, ":)");
  in.GreyscaleRGBA().Save("test-bilinear-resize-in.png");

  ImageRGBA big = in.ResizeBilinear(120, 120).GreyscaleRGBA();
  big.Save("test-bilinear-resize-out.png");
}

// XXX this just tests clipping; actually test the sampling!
static void TestSampleBilinear() {
  ImageF in(5, 5);
  in.Clear(1.0f);

  {
    // This sample is well outside the image, so it should
    // return the third arg.
    float out = in.SampleBilinear(10.0, 10.0, 0.25);
    CHECK(fabs(out - 0.25) < 0.00001);
  }
}

static void TestEq() {
  ImageA one(10, 20);
  one.Clear(0x7F);
  ImageA two = one;
  CHECK(two == one);
  two.SetPixel(3, 9, 0x11);
  CHECK(!(two == one));
}

static void TestScaleDown() {
  ImageRGBA img(10, 10);
  img.Clear32(0x00FF0000);
  img.SetPixel32(0, 0, 0xFF000001);
  img.SetPixel32(1, 0, 0x0000FF0F);
  img.SetPixel32(0, 1, 0x00FF00AA);
  // max value pixel
  img.SetPixel32(2, 0, 0xFFFFFFFF);
  img.SetPixel32(3, 0, 0xFFFFFFFF);
  img.SetPixel32(2, 1, 0xFFFFFFFF);
  img.SetPixel32(3, 1, 0xFFFFFFFF);

  img.BlendText32(2, 2, 0xFFFFFFFF, ":)");
  img.BlendLine32(9, 0, 0, 9, 0x00FFFFCC);
  img.BlendLine32(0, 4, 9, 9, 0xFFFF00EE);
  img.Save("test-scaledown-in.png");
  ImageRGBA out = img.ScaleDownBy(2);
  out.Save("test-scaledown-out.png");

  CHECK(out.Width() == img.Width() / 2);
  CHECK(out.Height() == img.Height() / 2);
  {
    auto [r, g, b, a] = out.GetPixel(0, 0);
    CHECK(a == (0xAA + 0x0F + 0x01 + 0x00) / 4);
    CHECK(r < 0x04);
    CHECK(g > 0x7F);
    CHECK(b < 0x20);
  }

  uint32 white = out.GetPixel32(1, 0);
  CHECK(white == 0xFFFFFFFF) << white;
}

static void TestLineEndpoints() {
  ImageRGBA img(10, 10);
  img.Clear32(0x000000FF);
  img.BlendLine32(1, 1, 3, 3, 0xFFFFFFFF);
  CHECK(img.GetPixel32(1, 1) == 0xFFFFFFFF);
  CHECK(img.GetPixel32(3, 3) == 0xFFFFFFFF);

  // TODO: Test other directions of lines
}

static void TestFilledCircle() {
  {
    ImageRGBA img(10, 10);
    img.Clear32(0x000000FF);
    // This should cover the entire image.
    img.BlendFilledCircle32(4, 4, 10, 0x23458A77);
    for (int y = 0; y < 10; y++) {
      for (int x = 0; x < 10; x++) {
        uint32_t color = img.GetPixel32(x, y);
        CHECK(color == 0x102040FF) << color;
      }
    }
  }

  {
    ImageRGBA img(10, 10);
    img.Clear32(0x000000FF);
    // This should cover the entire image.
    img.BlendFilledCircle32(4, 4, 1, 0x23458A77);
    uint32_t in_color = img.GetPixel32(4, 4);
    CHECK(in_color == 0x102040FF) << in_color;

    uint32_t out_color1 = img.GetPixel32(5, 5);
    CHECK(out_color1 == 0x000000FF);
    uint32_t out_color2 = img.GetPixel32(3, 5);
    CHECK(out_color2 == 0x000000FF);
  }
  
  // TODO: More circle tests
}

int main(int argc, char **argv) {
  TestBilinearResize();
  TestSampleBilinear();
  TestEq();
  TestScaleDown();
  TestLineEndpoints();
  TestFilledCircle();
  
  // TODO: More image tests!
  
  printf("OK\n");
  return 0;
}
