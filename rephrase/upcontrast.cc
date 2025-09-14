
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "color-util.h"
#include "image.h"
#include "threadutil.h"
#include "util.h"

static void FixOne(ImageRGBA *img) {
  for (int y = 0; y < img->Height(); y++) {
    for (int x = 0; x < img->Width(); x++) {
      const uint32_t color_orig = img->GetPixel32(x, y);
      // Do nothing to black.
      if (0 == (color_orig & ~0xFF)) continue;

      const auto &[r, g, b, a] = ColorUtil::U32ToFloats(color_orig);

      const auto &[h, s, v] = ColorUtil::RGBToHSV(r, g, b);

      float vv = std::max(v, 0.4f);

      uint32_t color_out = ColorUtil::HSVAToRGBA32(h, s, vv, a);
      img->SetPixel32(x, y, color_out);
    }
  }
}

int main(int argc, char **argv) {
  std::vector<std::string> files;
  for (const std::string &ent : Util::ListFiles(".")) {
    if (Util::isdir(ent)) continue;
    if (!Util::MatchesWildcard("*.png", ent)) continue;
    files.push_back(ent);
  }

  ParallelApp(
      files,
      [](const std::string &filename) {
        std::unique_ptr<ImageRGBA> img(ImageRGBA::Load(filename));
        CHECK(img.get() != nullptr);
        FixOne(img.get());
        img->Save(filename);
      },
      16);

  Print("Wrote {} files.\n", files.size());

  return 0;
}

