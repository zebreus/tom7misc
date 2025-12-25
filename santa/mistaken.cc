
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "periodically.h"
#include "status-bar.h"
#include "timer.h"
#include "util.h"

static constexpr bool FULL = false;

static void FindMistake() {
  std::vector<std::pair<BigInt, BigInt>> enc;
  for (const std::string &s :
         Util::NormalizeLines(Util::ReadFileToLines("encrypted.txt"))) {
    std::vector<std::string> c1c2 =
      Util::Split(s, ',');
    CHECK(c1c2.size() == 2);
    enc.emplace_back(BigInt(c1c2[0]), BigInt(c1c2[1]));
  }
  Print("Have {} encrypted messages.\n", enc.size());

  std::vector<BigInt> keys;
  for (const std::string &s :
         Util::NormalizeLines(Util::ReadFileToLines("pubs.txt"))) {
    BigInt key(s);
    CHECK(key > 0) << "Not a valid key: " << s;
    keys.push_back(std::move(key));
  }

  BigInt mistake(Util::ReadFile("mistaken.txt"));

  // Session public key
  BigInt session_pub(Util::ReadFile("session-pub.txt"));
  keys.push_back(session_pub);
  // erroneously sent
  keys.push_back(mistake);
  Print("Have {} keys.\n", keys.size());

  // System parameters.
  const BigInt SG = BigInt("97035875817856538071097984852143659850113040717180487875783371565273451246536742216736174708384786740533253396405747907150289525915530187570793986154826348563714376657128702681501301402031940740650862603724498556707966425139408498592844034405758710413665458781750608095937065069407231405292878119711821831393");
  const BigInt P = SG * 2 + 1;
  const BigInt G(7);
  const BigInt GG(7 * 7);

  auto IsKnownKey = [&](const BigInt &key) {
      for (const BigInt &inner : keys) {
        if (inner == key) {
          return true;
        }
      }

      return false;
    };

  auto GeneratesPublic = [&](const BigInt &prv) ->
    std::optional<BigInt> {
      BigInt pub = BigInt::PowMod(G, prv * 2, P);
      if (IsKnownKey(pub))
        return {pub};
      return std::nullopt;
    };

  // Is any key actually the public key for another?
  for (const BigInt &prv : keys) {
    if (std::optional<BigInt> pub = GeneratesPublic(prv)) {
      Print("pub({}) = {}!\n",
            prv, pub.value());
      return;
    }
  }

  Timer timer;
  StatusBar status(1);
  Periodically status_per(2.0);
  BigInt pub(1); // BigInt::PowMod(G, BigInt(0), P);
  constexpr int64_t LIMIT = FULL ? 100'000'000 : 100000;
  for (int64_t prv = 0; prv < LIMIT; prv++) {
    // BigInt prv(i);
    // BigInt pub = BigInt::PowMod(G, prv * 2, P);

    pub = BigInt::Mod(pub * GG, P);

    if (pub == mistake) {
      Print("brute: pub({}) = {}!\n",
            prv, mistake);
      return;
    }

    status_per.RunIf([&]{
        double sec = timer.Seconds();
        int64_t done = prv;
        status.Progress(prv, LIMIT, "At {}. {:.1f}/sec. {} total.",
                        prv,
                        done / sec,
                        ANSI::Time(sec));
      });
  }

  // Decrypt each message using each key
  for (const auto &[c1, c2] : enc) {
    for (const BigInt &private_key : keys) {
      BigInt s = BigInt::PowMod(c1, private_key * 2, P);
      std::optional<BigInt> si = BigInt::ModInverse(s, P);
      if (si.has_value()) {
        BigInt msg = (c2 * si.value()) % P;

        // Is it one of the keys?
        if (IsKnownKey(msg)) {
          Print("A message decodes using private key {} to\n"
                "the known public key {}!\n",
                private_key, msg);
        }

        // Is it a private key?
        if (std::optional<BigInt> pub = GeneratesPublic(msg)) {
          Print("A message decodes using private key {} to {},\n"
                "which is the private key for {}!\n",
                private_key, msg, pub.value());
        }
      }

    }
  }

  Print("Nothing found...\n");
}



int main(int argc, char **argv) {
  ANSI::Init();

  FindMistake();

  return 0;
}
