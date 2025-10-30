
#ifndef _MULTI_RSA_H
#define _MULTI_RSA_H

#include <optional>
#include <string>
#include <vector>

#include "bignum/big.h"

struct MultiRSA {

  struct PrimeFactor {
    // The factor.
    BigInt p;
    // CRT parameters:
    // The exponent, which is d mod (p - 1).
    BigInt exp;
    // The modular inverse: (p1 * ...* p_(n-1))⁻¹ mod p_n.
    BigInt inv;
  };

  struct Key {
    // Public:
    BigInt n;
    BigInt e;

    // Private:
    // Encryption key.
    BigInt d;

    std::vector<PrimeFactor> factors;
  };

  static std::optional<Key> KeyFromPrimes(std::vector<BigInt> primes);

  static bool ValidateKey(const Key &key, std::string *err = nullptr);

  static std::vector<uint8_t> EncodePKCS1(const Key &key);
  static std::vector<uint8_t> EncodePKCS8(const Key &key);

  // From the binary ASN.1 DER format.
  static std::optional<Key> DecodePKCS1(std::span<const uint8_t> contents);
  static std::optional<Key> DecodePKCS8(std::span<const uint8_t> contents);

  static bool KeyEq(const Key &a, const Key &b);
};

#endif
