

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "color-util.h"
#include "font-image.h"
#include "image.h"
#include "util.h"

static void RenderQuantiles() {
  std::unique_ptr<BitmapFont> font =
    BitmapFont::Load("../../bit7/fixedersys2x.cfg");

  std::vector<double> quantiles;
  for (const std::string &line :
         Util::ReadFileToLines("../scube-score-quantiles.txt")) {
    std::optional<double> od = Util::ParseDoubleOpt(line);
    if (od.has_value()) {
      quantiles.push_back(od.value());
    }
  }

  ImageRGBA img(1920, 1080);

  int64_t rows = img.Height() / font->Height();
  for (int r = 0; r < rows; r++) {
    int ypos = r * font->Height();
    double f = r / (double)rows;
    int q = std::clamp((int)(f * quantiles.size()),
                       0, (int)quantiles.size() - 1);
    font->DrawText(&img, 16, ypos,
                   ColorUtil::LinearGradient32(ColorUtil::VISIBLE_SPECTRUM,
                                               f),
                   std::format("{:.8g}", quantiles[q]));
  }

  img.ScaleBy(2).Save("quantiles.png");
  Print("Wrote " AGREEN("quantiles.png") "\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  RenderQuantiles();

  return 0;
}
