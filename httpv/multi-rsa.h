
#ifndef _MULTI_RSA_H
#define _MULTI_RSA_H

#include <optional>
#include <span>
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

  // Size of ciphertext and padded plaintext in bytes.
  static int BlockSize(const Key &key);
  // Same from just the public modulus n.
  static int BlockSize(const BigInt &modulus);

  // Treats the span of bytes as a BigInt and performs the decryption
  // operation in place. Aborts if the ciphertext is the wrong size or
  // the key is very invalid (e.g. zero modulus), but otherwise this
  // does not perform validation.
  static void RawDecryptInPlace(const Key &key, std::span<uint8_t> buffer);
  static void RawDecryptInPlaceCRT(const Key &key, std::span<uint8_t> buffer);

  // Treats the span of bytes as a BigInt and performs the encryption
  // operation in place. Aborts if the plaintext is the wrong size or
  // larger than the modulus, or the key is very invalid.
  static void RawEncryptInPlace(const BigInt &n, const BigInt &e,
                                std::span<uint8_t> buffer);

  // Extract a valid padded PCKS#1 message from a decrypted buffer.
  // Returns nullopt if the message is invalid. (Note: Never expose
  // that the padding was invalid to an attacker; this would
  // permit padding oracle attacks and leak key bits.)
  static std::optional<std::span<const uint8_t>> ExtractPadded(
      std::span<const uint8_t> buffer);

  // Encode the full key in a standard format.
  static std::vector<uint8_t> EncodePKCS1(const Key &key);
  static std::vector<uint8_t> EncodePKCS8(const Key &key);

  // From the binary ASN.1 DER format.
  static std::optional<Key> DecodePKCS1(std::span<const uint8_t> contents);
  static std::optional<Key> DecodePKCS8(std::span<const uint8_t> contents);

  // Generates a PKCS#1 v1.5 signature for the given 32-byte SHA-256 hash.
  // Requires that the key is at least 62 bytes.
  static std::vector<uint8_t> SignSHA256(
      const Key &key, std::span<const uint8_t> hash);

  static bool KeyEq(const Key &a, const Key &b);
};

#endif
