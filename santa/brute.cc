
#include <string>
#include <utility>
#include <vector>
#include <cstdint>

#include "ansi.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "periodically.h"
#include "status-bar.h"
#include "timer.h"
#include "base/print.h"
#include "util.h"

// constexpr int64_t START = int64_t{895379980437};
constexpr int64_t START = int64_t{1669857164696};

static void Brute(const std::vector<BigInt> &target_pubs) {
  const BigInt SG = BigInt("97035875817856538071097984852143659850113040717180487875783371565273451246536742216736174708384786740533253396405747907150289525915530187570793986154826348563714376657128702681501301402031940740650862603724498556707966425139408498592844034405758710413665458781750608095937065069407231405292878119711821831393");
  const BigInt P = SG * 2 + 1;
  const BigInt G(7);

  const BigInt GG(7 * 7);

  Print("Number of bits in P: {}\n", BigInt::NumBits(P));

  Timer timer;
  Periodically status_per(1);
  StatusBar status(1);

  BigInt pub = BigInt::PowMod(G, BigInt(START * 2), P);
  for (int64_t prv = START; true; prv++) {
    // BigInt prv(i);
    // BigInt pub = BigInt::PowMod(G, prv * 2, P);

    // Exponent increase by 2, which is two more factors of G.
    // PERF: I think we could just do GT and subtract here.
    pub = BigInt::Mod(pub * GG, P);

    for (const BigInt &target_pub : target_pubs) {
      if (pub == target_pub) {
        Print(AGREEN("FOUND") ":\n\n");
        Print("Private: {}\n\nPublic: {}",
              prv, pub.ToString());
        return;
      }
    }

    status_per.RunIf([&]{
        double sec = timer.Seconds();
        int64_t done = prv - START;
        status.Status("At {}. {:.1f}/sec. {} total.",
                      prv,
                      done / sec,
                      ANSI::Time(sec));
      });
  }
}


int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 2);

  std::vector<BigInt> pubs;
  for (const std::string &s :
         Util::NormalizeLines(Util::ReadFileToLines(argv[1]))) {
    BigInt pub(s);
    CHECK(pub > 0) << "Not a valid public key: " << s;
    pubs.push_back(std::move(pub));
  }

  Brute(pubs);

  Print("\nOK");
  return 0;
}
