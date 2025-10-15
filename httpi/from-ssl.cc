
#include "ansi.h"

#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "multi-rsa.h"
#include "pem.h"
#include "util.h"

static void ParseSSL(std::string_view filename) {
  std::vector<std::string> lines = Util::ReadFileToLines(filename);
  CHECK(!lines.empty()) << filename;

  std::unordered_map<std::string, BigInt> m;

  for (int i = 0; i < lines.size(); /* in loop */) {
    std::string_view line(lines[i]);
    // Expect a line like "key:"
    auto colon = line.find(':');
    CHECK(colon != std::string_view::npos) << line;
    std::string_view key = line.substr(0, colon);
    if (key == "Private-Key" ||
        key == "publicExponent") {
      i++;
      continue;
    }

    // Otherwise, read bytes.
    BigInt big(0);
    i++;
    while (i < lines.size() && lines[i][0] == ' ') {
      std::string line = Util::NormalizeWhitespace(lines[i]);

      std::vector<std::string> bytes = Util::Split(line, ':');
      for (const std::string &byte : bytes) {
        if (!byte.empty()) {
          CHECK(byte.size() == 2 &&
                Util::IsHexDigit(byte[0]) &&
                Util::IsHexDigit(byte[1])) << line;
          int b = Util::HexDigitValue(byte[0]) * 16 +
            Util::HexDigitValue(byte[1]);
          big *= 256;
          big += b;
        }
      }
      i++;
    }

    m[std::string(key)] = std::move(big);
  }

  for (const auto &[k, v] : m) {
    Print("{}: {}\n", k, v.ToString());
  }

  auto Required = [&m](std::string_view k) {
      auto it = m.find(std::string(k));
      CHECK(it != m.end()) << k;
      return it->second;
    };
  MultiRSA::Key key;
  key.n = Required("modulus");
  key.e = BigInt{65537};
  key.d = Required("privateExponent");

  MultiRSA::PrimeFactor p, q;
  q.p = Required("prime1");
  q.exp = Required("exponent1");
  q.inv = Required("coefficient");

  p.p = Required("prime2");
  p.exp = Required("exponent2");
  p.inv = BigInt{1};

  key.factors.emplace_back(std::move(p));
  key.factors.emplace_back(std::move(q));
  for (int i = 2; true; i++) {
    auto it = m.find(std::format("prime{}", i + 1));
    if (it == m.end()) break;
    MultiRSA::PrimeFactor f;
    f.p = it->second;
    f.exp = Required(std::format("exponent{}", i + 1));
    f.inv = Required(std::format("coefficient{}", i + 1));
    key.factors.emplace_back(std::move(f));
  }

  std::string error;
  CHECK(MultiRSA::ValidateKey(key, &error)) << error;
  Print("\n\n");
  Util::WriteFile("multi-round-trip.pem",
                  PEM::ToPEM(MultiRSA::EncodePKCS8(key), "PRIVATE KEY"));
  Print("Wrote multi-round-trip.pem. OK\n");

}

int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 2) << "./from-ssl.exe dump.ssl";

  ParseSSL(argv[1]);

  return 0;
}
