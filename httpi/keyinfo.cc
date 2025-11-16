
#include <string_view>
#include <string>
#include <algorithm>

#include "ansi.h"
#include "pem.h"
#include "multi-rsa.h"
#include "util.h"
#include "base/logging.h"
#include "base/print.h"
#include "bignum/big.h"

static void KeyInfo(std::string_view keyfile) {
  std::string pem = Util::ReadFile(keyfile);

  std::vector<std::vector<uint8_t>> keys = PEM::ParsePEMs(pem, "PRIVATE KEY");
  CHECK(keys.size() == 1) << "Expected a PEM file with exactly one "
    "private key in '" << keyfile << "', but got " << keys.size();

  std::optional<MultiRSA::Key> okey = MultiRSA::DecodePKCS8(keys[0]);
  CHECK(okey.has_value()) << "Couldn't parse key (PKCS8) from " << keyfile;

  const MultiRSA::Key &key = okey.value();

  size_t fewest_bits = BigInt::NumBits(key.n);
  for (const MultiRSA::PrimeFactor &factor : key.factors) {
    fewest_bits = std::min(BigInt::NumBits(factor.p), fewest_bits);
  }

  Print("modulus size: {} bits\n"
        "block size: {} bytes\n"
        "num factors: {}\n"
        "size of smallest factor: {} bits\n"
        "n: {}\n"
        "e: {}\n"
        "d: {}\n",
        BigInt::NumBits(key.n),
        MultiRSA::BlockSize(key),
        key.factors.size(),
        fewest_bits,
        key.n.ToString(),
        key.e.ToString(),
        key.d.ToString());

  for (int i = 0; i < key.factors.size(); i++) {
    const MultiRSA::PrimeFactor &factor = key.factors[i];
    Print("p{}: {}\n"
          "exp{}: {}\n"
          "inv{}: {}\n",
          i, factor.p.ToString(),
          i, factor.exp.ToString(),
          i, factor.inv.ToString());
  }

  std::string error;
  if (MultiRSA::ValidateKey(key, &error)) {
    Print("Key is " AGREEN("valid") ".\n");
  } else {
    Print("Key is " ARED("invalid") ": {}\n", error);
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 2) << "Usage:\n   keyinfo.exe private.key\n";

  KeyInfo(argv[1]);

  return 0;
}
