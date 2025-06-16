
#include <cstdint>
#include <cstdio>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "randutil.h"
#include "threadutil.h"
#include "util.h"

using uint8 = uint8_t;
using uint32 = uint32_t;

static bool Okay(const ImageRGBA &database,
                 const std::vector<uint8> &mem) {

  for (int addr = 0; addr < 2048; addr++) {
    uint32_t c = database.GetPixel32(addr, mem[addr]);
    if (c == 0xFF0000FF) return false;
  }
  return true;
}

int main(int argc, char **argv) {
  std::unique_ptr<ImageRGBA> database(ImageRGBA::Load("database.png"));
  CHECK(database.get() != nullptr);

  // Get original memory values. Assumes database is well-formed.
  std::vector<uint8> orig(2048, 0);
  for (int addr = 0; addr < 2048; addr++) {
    for (int value = 0; value < 256; value++) {
      uint32 color = database->GetPixel32(addr, value);
      if (color == 0x0000FFFF) {
        orig[addr] = value;
        break;
      }
    }
  }

  // ArcFour rc("stochastic");

  static constexpr int DENOM = 8192;
  static constexpr int MAX_DENOM = DENOM / 8;
  static constexpr int HEIGHT = 256;
  static constexpr int NUM_TRIALS = 65536;

  std::mutex m;
  ImageRGBA out(MAX_DENOM, HEIGHT);
  std::map<int, int> count_ok;
  out.Clear32(0x000000FF);

  ParallelComp(
      MAX_DENOM,
      [&](int numer) {
        ArcFour rc(std::format("stochastic.{}", numer));

        // Chance of randomly flipping a bit.
        const double prob = numer / (double)DENOM;
        int ok = 0;
        for (int trial = 0; trial < NUM_TRIALS; trial++) {
          std::vector<uint8> mem = orig;
          for (int addr = 0; addr < 2048; addr++) {
            uint8 byte = mem[addr];
            for (int bit = 0; bit < 8; bit++) {
              if (RandDouble(&rc) < prob) {
                byte ^= (1 << bit);
              }
            }
            mem[addr] = byte;
          }

          // XXX also test if we successfully flipped the targets
          if (Okay(*database, mem)) {
            ok++;
            // out.SetPixel32(numer, trial, 0xFF0000FF);
          }
        }

        {
          MutexLock ml(&m);
          count_ok[numer] = ok;
          for (int y = 0; y < HEIGHT; y++) {
            uint32_t c =
              (y / (double)HEIGHT < ok / (double)NUM_TRIALS) ?
              0x00FF00FF : 0x330000FF;
            out.SetPixel32(numer, y, c);
          }
        }

        if ((numer & 15) == 0) {
          printf("%d/%d\n", numer, MAX_DENOM);
        }
      },
      12);

  out.Save("stochastic.png");

  std::string tsv;
  for (const auto &[numer, ok] : count_ok) {
    AppendFormat(&tsv, "{:.7f}\t{:.7f}\n",
                 numer / (double)DENOM,
                 ok / (double)NUM_TRIALS);
  }

  Util::WriteFile("stochastic.tsv", tsv);

  return 0;
}
