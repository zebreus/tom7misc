
#include "rsa.h"

#include <cstdint>
#include <optional>
#include <utility>

#include "base/logging.h"
#include "base/print.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "crypt/cryptrand.h"

BigInt RSA::GeneratePrime(int bits, CryptRand *cr) {
  auto Rand = [&]() { return cr->Word64(); };

  BigInt top = BigInt::LeftShift(BigInt{1}, bits);
  BigInt msb = BigInt::LeftShift(BigInt{1}, bits - 1);
  for (;;) {
    // PERF: Since we know we're generating up to a power of two,
    // we don't need the full generality of RandTo here.
    BigInt x = BigInt::RandTo(Rand, top);

    x = BigInt::BitwiseOr(std::move(x), msb);
    x = BigInt::BitwiseOr(std::move(x), BigInt::RightShift(msb, 1));

    // Print("Try {}.\n", n.ToString());

    CHECK(x > 0);

    // Start with an odd number.
    if (x.IsEven()) {
      x = x + 1;
    }

    while (x < top) {
      if (BigInt::IsProbablyPrime(x, 64)) {
        return x;
      }
      x += 2;
    }
  }
}


RSA::Key RSA::GenerateKey(int bits, CryptRand *cr) {
  const int prime_bits = bits / 2;
  for (;;) {
    // Two distinct large prime numbers.
    BigInt p = GeneratePrime(prime_bits, cr);
    BigInt q = GeneratePrime(prime_bits, cr);
    if (p == q) [[unlikely]] continue;

    auto ko = KeyFromPrimes(std::move(p), std::move(q));
    if (ko.has_value()) return std::move(ko.value());
  }
}

std::optional<RSA::Key> RSA::KeyFromPrimes(BigInt p_in, BigInt q_in) {
  static constexpr bool VERBOSE = false;

  RSA::Key key;
  key.p = std::move(p_in);
  key.q = std::move(q_in);

  if (VERBOSE) {
    Print("p: {}\n"
          "q: {}\n",
          key.p.ToString(), key.q.ToString());
  }

  // Modulus.
  key.n = key.p * key.q;
  if (VERBOSE) {
    Print("n: {}\n", key.n.ToString());
  }

  // Euler's totient φ(n) = (p - 1) * (q - 1).
  BigInt phi_n = (key.p - 1) * (key.q - 1);

  // Public exponent 'e'. 65537 is conventional; it
  // is fast to multiply because x * 65537 = (x << 16) + x.
  // Must have 1 < e < φ(n) and gcd(e, φ(n)) == 1.
  key.e = BigInt(65537);

  // e is not coprime with φ(n), which is very unlikely.
  if (BigInt::GCD(key.e, phi_n) != 1)
    return std::nullopt;

  // Private exponent 'd', the modular inverse of e mod φ(n).
  {
    std::optional<BigInt> d_opt = BigInt::ModInverse(key.e, phi_n);
    CHECK(d_opt.has_value()) << "e⁻¹ mod φ(n) should exist when e and φ(n) "
      "are coprime, which we just checked.";
    key.d = std::move(d_opt.value());
  }

  key.exp1 = BigInt::CMod(key.d, key.p - 1);
  key.exp2 = BigInt::CMod(key.d, key.q - 1);

  {
    std::optional<BigInt> qi_opt = BigInt::ModInverse(key.q, key.p);
    // e.g. if p = q.
    if (!qi_opt.has_value()) return std::nullopt;
    key.qinv = std::move(qi_opt.value());
  }

  return key;
}
