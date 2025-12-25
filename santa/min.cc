
#include <cstdint>
#include <ctime>
#include <format>

#include "ansi.h"
#include "arcfour.h"
#include "base/print.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "periodically.h"
#include "status-bar.h"
#include "timer.h"

static void BruteMin() {
  const BigInt SG = BigInt("97035875817856538071097984852143659850113040717180487875783371565273451246536742216736174708384786740533253396405747907150289525915530187570793986154826348563714376657128702681501301402031940740650862603724498556707966425139408498592844034405758710413665458781750608095937065069407231405292878119711821831393");
  const BigInt P = SG * 2 + 1;
  const BigInt G(7);

  const BigInt GG(7 * 7);

  Timer timer;
  Periodically status_per(5);
  StatusBar status(1);

  ArcFour rc(std::format("min.{}", time(nullptr)));
  auto Rand = [&rc]{
      return rc.Word64();
    };

  BigInt prv = (P >> 4) + BigInt::RandTo(Rand, P >> 1);
  BigInt pub = BigInt::PowMod(G, prv * 2, P);

  bool has_new = false;
  int64_t done = 0;

  BigInt best_prv = prv;
  BigInt best_pub = pub;
  int best_bits = BigInt::NumBits(best_pub);
  for (;;) {
    ++prv;
    pub = BigInt::CMod(pub * GG, P);

    if (pub < best_pub) {
      best_prv = prv;
      best_pub = pub;
      has_new = true;
    }
    done++;

    status_per.RunIf([&]{
        if (has_new) {
          best_bits = BigInt::NumBits(best_pub);
          status.Print("New best: " ARED("{}") " → " AGREEN("{}") " ("
                       AWHITE("{}") " bits)\n",
                       best_prv.ToString(), best_pub.ToString(),
                       best_bits);
          has_new = false;
        }
        double sec = timer.Seconds();
        status.Status("Best: {} bits. Tried {}. {:.1f}/sec. {} total.",
                      best_bits,
                      done,
                      done / sec,
                      ANSI::Time(sec));
      });
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  BruteMin();

  Print("\nOK");
  return 0;
}
