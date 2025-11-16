
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "bignum/big.h"
#include "crypt/cryptrand.h"

#include "asn1.h"
#include "multi-rsa.h"
#include "pem.h"
#include "rsa.h"

static MultiRSA::Key Generate(int num_factors, int bits, CryptRand *cr) {

  for (;;) {
    std::vector<BigInt> factors;
    int bits_left = bits;
    int factors_left = num_factors;
    while (factors_left > 0) {
      const int prime_bits = bits_left / factors_left;
      BigInt p = RSA::GeneratePrime(prime_bits, cr);
      bits_left -= BigInt::NumBits(p);
      factors_left--;
      factors.emplace_back(std::move(p));
    }

    auto ko = MultiRSA::KeyFromPrimes(std::move(factors));
    if (ko.has_value()) {
      if (BigInt::NumBits(ko.value().n) == bits) {
        CHECK(MultiRSA::ValidateKey(ko.value()));
        return std::move(ko.value());
      } else {
        Print("Not enough bits.\n");
      }
    }
  }
}

static MultiRSA::Key GenerateBad(int num_factors, int bits, CryptRand *cr) {

  for (;;) {
    std::vector<BigInt> factors;
    int bits_left = bits;
    int factors_left = num_factors;
    while (factors_left > 0) {
      const int prime_bits = bits_left / factors_left;
      BigInt p = RSA::GeneratePrime(prime_bits, cr);
      bits_left -= BigInt::NumBits(p);
      factors_left--;
      factors.emplace_back(std::move(p));
    }

    auto ko = MultiRSA::KeyFromPrimes(std::move(factors));
    if (ko.has_value()) {
      if (BigInt::NumBits(ko.value().n) == bits) {
        CHECK(MultiRSA::ValidateKey(ko.value()));
        return std::move(ko.value());
      } else {
        Print("Not enough bits.\n");
      }
    }
  }
}



static void Generate() {
  CryptRand cr;
  MultiRSA::Key key = Generate(3, 128, &cr);

  Print(stderr,
        "--------\n"
        "num factors: {}\n"
        "n: {}\n"
        "e: {}\n"
        "d: {}\n",
        key.factors.size(),
        key.n.ToString(),
        key.e.ToString(),
        key.d.ToString());

  for (int i = 0; i < key.factors.size(); i++) {
    const MultiRSA::PrimeFactor &factor = key.factors[i];
    Print(stderr,
          "p{}: {}\n"
          "exp{}: {}\n"
          "inv{}: {}\n",
          i, factor.p.ToString(),
          i, factor.exp.ToString(),
          i, factor.inv.ToString());
  }


  Print(stderr, "n bits: {}\n", BigInt::NumBits(key.n));

  std::string pem = PEM::ToPEM(MultiRSA::EncodePKCS8(key), "PRIVATE KEY");
  Print("{}\n", pem);
}

int main(int argc, char **argv) {
  ANSI::Init();

  Generate();

  return 0;
}
