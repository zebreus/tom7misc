
#include "multi-rsa.h"

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "asn1.h"
#include "base/logging.h"
#include "base/print.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"

std::optional<MultiRSA::Key> MultiRSA::KeyFromPrimes(
    std::vector<BigInt> primes) {
  static constexpr bool VERBOSE = false;

  MultiRSA::Key key;

  BigInt n{1};
  for (const BigInt &p : primes) n *= p;
  key.n = std::move(n);
  if (VERBOSE) {
    Print("n: {}\n", key.n.ToString());
  }

  // Ancestral RSA uses Euler's totient
  //   φ(n) = (p1 - 1) * (p2 - 1) * ... * (pk - 1).
  // OpenSSL seems to insist on the "Carmichael function":
  //   λ(n) = LCM(p1 - 1, p2 - 1, ... pk - 1)
  //        = φ(n) / GCD(p1 - 1, p2 - 1, ... pk - 1)
  // ... which results in a smaller exponent 'd'.
  BigInt lambda_n{1};
  for (const BigInt &p : primes)
    lambda_n = BigInt::LCM(std::move(lambda_n), p - 1);

  // Public exponent 'e'. 65537 is conventional (and many
  // implementations require it); it is fast to multiply because
  // x * 65537 = (x << 16) + x.
  // Must have 1 < e < λ(n) and gcd(e, λ(n)) == 1.
  key.e = BigInt(65537);

  // e is not coprime with λ(n). This happens if p-1 is divisible
  // by 65537 for any of the factors, which happens about 1/65537 of
  // the time. We can't generate a key with these factors.
  if (BigInt::GCD(key.e, lambda_n) != 1)
    return std::nullopt;

  // Private exponent 'd', the modular inverse of e mod φ(n).
  {
    std::optional<BigInt> d_opt = BigInt::ModInverse(key.e, lambda_n);
    CHECK(d_opt.has_value()) << "e⁻¹ mod λ(n) should exist when e and λ(n) "
      "are coprime, which we just checked.";
    key.d = std::move(d_opt.value());
  }

  BigInt product{1};
  for (BigInt &p : primes) {
    PrimeFactor f;
    f.exp = BigInt::CMod(key.d, p - 1);

    std::optional<BigInt> qi_opt = BigInt::ModInverse(product, p);
    // e.g. if p = q.
    if (!qi_opt.has_value()) return std::nullopt;
    f.inv = std::move(qi_opt.value());

    product *= p;
    f.p = std::move(p);
    key.factors.emplace_back(std::move(f));
  }

  CHECK(key.factors.size() == primes.size());
  return key;
}

bool MultiRSA::ValidateKey(const MultiRSA::Key &key, std::string *err) {
  if (key.factors.size() < 2) {
    if (err != nullptr) *err = "Need at least two factors.";
    return false;
  }

  {
    BigInt n{1};
    for (const PrimeFactor &factor : key.factors) {
      n *= factor.p;
    }
    if (key.n != n) {
      if (err != nullptr)
        *err = "Modulus 'n' is not the product of the factors.";
      return false;
    }
  }


  BigInt lambda_n{1};
  for (const PrimeFactor &factor : key.factors) {
    lambda_n = BigInt::LCM(std::move(lambda_n), factor.p - 1);
  }
  if ((key.e * key.d) % lambda_n != 1) {
    if (err != nullptr)
      *err = "Public / private exponents invalid: (e * d) mod λ(n) != 1";
    return false;
  }

  BigInt product{1};
  for (size_t i = 0; i < key.factors.size(); ++i) {
    const PrimeFactor &factor = key.factors[i];

    if (factor.exp != key.d % (factor.p - 1)) {
      if (err != nullptr)
        *err = std::format("Wrong Exponent for factor #{}.",
                           i, factor.p.ToString());
      return false;
    }

    std::optional<BigInt> inv_check_opt =
      BigInt::ModInverse(product, factor.p);

    if (!inv_check_opt.has_value()) {
      if (err != nullptr) {
        *err = std::format("No modular inverse! #{} (p={}). "
                           "Perhaps a repeated factor?",
                           i, factor.p.ToString());
      }
      return false;
    }
    if (factor.inv != inv_check_opt.value()) {
      if (err != nullptr) {
        *err = std::format("Exponent for factor #{} (p={}) is "
                           "incorrect.", i, factor.p.ToString());
      }
      return false;
    }

    product *= factor.p;
  }

  // TODO: Check primality of factors. Do an encryption/decryption.
  return true;
}

std::vector<uint8_t> MultiRSA::EncodePKCS1(const MultiRSA::Key &key) {
  CHECK(key.factors.size() >= 2) << "Need at least two factors.";

  const bool is_multi = key.factors.size() > 2;

  // Factors past the first 2 are encoded in an optional sequence
  // at the end. It must be omitted if empty.
  std::vector<uint8_t> other_primes;
  if (is_multi) {
    for (int i = 2; i < key.factors.size(); i++) {
      const MultiRSA::PrimeFactor &factor = key.factors[i];
      std::vector<uint8_t> encoded =
        ASN1::EncodeSequence(ASN1::Concat(
                                 ASN1::EncodeInt(factor.p),
                                 ASN1::EncodeInt(factor.exp),
                                 ASN1::EncodeInt(factor.inv)));
      other_primes.insert(other_primes.end(),
                          encoded.begin(), encoded.end());
    }
    // A sequence of sequences.
    other_primes = ASN1::EncodeSequence(std::move(other_primes));
    // other_primes = ASN1::EncodeContextSpecific(0, std::move(other_primes));
  }

  return ASN1::EncodeSequence(ASN1::Concat(
        // Version 1 = MultiRSA.
        // If there are only 2 factors, you must use 0 = RSA.
        ASN1::EncodeInt(BigInt{is_multi ? 1 : 0}),
        ASN1::EncodeInt(key.n),
        ASN1::EncodeInt(key.e),
        ASN1::EncodeInt(key.d),
        // The factors are not stored uniformly; the first
        // two, called "p" and "q", are stored in their old
        // fields so that the structure extends the RSA format.
        // Careful: RSA wants q⁻¹ mod p below, so we set
        // q = factors[0] and p = factors[1].
        ASN1::EncodeInt(key.factors[1].p),
        ASN1::EncodeInt(key.factors[0].p),
        ASN1::EncodeInt(key.factors[1].exp),
        ASN1::EncodeInt(key.factors[0].exp),
        // factors[0] inv is always 1; not stored
        ASN1::EncodeInt(key.factors[1].inv),
        other_primes));
}

std::vector<uint8_t> MultiRSA::EncodePKCS8(const MultiRSA::Key &key) {

  [[maybe_unused]] const bool is_multi = key.factors.size() > 2;

  std::vector<uint8_t> algorithm_identifier =
    ASN1::EncodeSequence(
        ASN1::Concat(
            // OID for rsaEncryption is 1.2.840.113549.1.1.1
            ASN1::EncodeOID({1, 2, 840, 113549, 1, 1, 1}),
            ASN1::EncodeNull()));

  return ASN1::EncodeSequence(
      ASN1::Concat(
          // Version = 0, even with multi-key RSA.
          ASN1::EncodeInt(BigInt{0}),
          algorithm_identifier,
          ASN1::EncodeOctetString(EncodePKCS1(key))));
}
