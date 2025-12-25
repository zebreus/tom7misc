
#include <format>
#include <string>

#include "ansi.h"
#include "base/print.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "image.h"

static bool IsQr(const BigInt &SG, const BigInt &P, const BigInt &n) {
  if (n <= 0 || n > P)
    return false;

  // Euler's Criterion: n is a residue iff n^((p-1)/2) mod p is 1.
  // (p-1)/2 is just the Sophie Germain prime SG (this is not a
  // coincidence!)
  const BigInt res = BigInt::PowMod(n, SG, P);
  if (res == 1) {
    return true;
  } else {
    CHECK(res == P - 1);
    return false;
  }
}

static void GenQR() {
  BigInt SG(48413);
  BigInt P = 2 * SG + 1;

  BigInt G(5);

  const int WIDTH = 312;
  CHECK(WIDTH * WIDTH >= P);
  ImageRGBA img(WIDTH, WIDTH);
  img.Clear32(0x00000000);
  for (int i = 1; i < P; i++) {
    int y = i / WIDTH;
    int x = i % WIDTH;

    img.SetPixel32(x, y, IsQr(SG, P, BigInt(i)) ?
                   0xFFFFFFFF : 0x000000FF);
  }

  std::string filename = std::format("qr-{}.png", P);
  img.Save(filename);
  Print("Wrote {}\n", filename);
}

int main(int argc, char **argv) {
  ANSI::Init();

  GenQR();

  return 0;
}
