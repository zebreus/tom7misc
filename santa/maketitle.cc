
#include <format>
#include <string>
#include <memory>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "font-image.h"
#include "image.h"

static void MakeTitle() {
  std::unique_ptr<BitmapFont> font =
    BitmapFont::Load("../bit7/fixedersys2x.cfg");

  std::unique_ptr<ImageRGBA> santa(ImageRGBA::Load("santa.png"));
  CHECK(santa.get() != nullptr);

  const int CH = font->Height() - 1;

  ImageRGBA img(santa->Width() * font->Width() * 2,
                santa->Height() * CH);
  img.Clear32(0x000000FF);

  BigInt fact(1);
  for (int i = 2; i <= 2025; i++) {
    fact *= i;
  }

  std::string digits = fact.ToString();
  Print("Need {} digits; have {}.\n",
        santa->Height() * santa->Width() * 2,
        digits.size());

  int d = 0;
  for (int y = 0; y < santa->Height(); y++) {
    const int ypos = CH * y;
    for (int x = 0; x < santa->Width(); x++) {
      const int xpos = x * font->Width() * 2;
      uint32_t c = santa->GetPixel32(x, y);

      char d1 = digits[d++];
      char d2 = digits[d++];

      font->DrawText(&img, xpos, ypos,
                     c,
                     std::format("{:c}{:c}", d1, d2));
    }
  }

  ImageRGBA out(1280, 720);
  out.Clear32(0x000000FF);
  out.CopyImage((out.Width() - img.Width()) / 2,
                (out.Height() - img.Height()) / 2,
                img);

  std::string filename = "thumb.png";
  out.Save(filename);
  Print("Wrote {}\n", filename);
}

int main(int argc, char **argv) {
  ANSI::Init();

  MakeTitle();

  return 0;
}
