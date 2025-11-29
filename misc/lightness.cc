
#include <memory>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "image.h"

static void Lightness(std::string_view infile,
                      std::string_view outfile) {
  std::unique_ptr<ImageRGBA> img(ImageRGBA::Load(infile));
  CHECK(img.get() != nullptr) << infile;
  ImageA lightness = img->Lightness();

  lightness.GreyscaleRGBA().Save(outfile);
  Print("Wrote {}\n", outfile);
}

int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 3) << "./lightness.exe image.png out.png";
  Lightness(argv[1], argv[2]);

  return 0;
}
