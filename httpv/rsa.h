
#ifndef _RSA_H
#define _RSA_H

#include "bignum/big.h"
#include "crypt/cryptrand.h"
#include <optional>

struct RSA {

  struct Key {
    // Public:
    BigInt n;
    BigInt e;

    // Private:
    // Encryption key.
    BigInt d;

    BigInt p;
    BigInt q;
    // d mod (p - 1)
    BigInt exp1;
    // d mod (q - 1)
    BigInt exp2;
    // q⁻¹ mod p
    BigInt qinv;
  };

  // Generates a random odd prime number with the specified number of
  // bits. The top two bits are set, such that the product of two
  // such numbers is guaranteed to have 2^(2*bits) bits.
  static BigInt GeneratePrime(int bits, CryptRand *cr);

  // Generates an RSA key pair with the given bit size.
  static Key GenerateKey(int bits, CryptRand *cr);

  static std::optional<Key> KeyFromPrimes(BigInt p, BigInt q);
};

#endif
