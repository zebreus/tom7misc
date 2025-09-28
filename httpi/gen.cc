
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "base64.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "crypt/cryptrand.h"

#include "asn1.h"

struct RSAKey {
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

// Generates a random prime number with a specified number of bits.
BigInt GeneratePrime(int bits, CryptRand *cr) {
  auto Rand = [&]() { return cr->Word64(); };

  for (;;) {
    // We want a random number in the range [2^(bits-1), 2^bits - 1].
    BigInt bottom = BigInt::LeftShift(BigInt{1}, bits - 1);
    BigInt top = BigInt::LeftShift(BigInt{1}, bits);

    BigInt n = bottom + BigInt::RandTo(Rand, top - bottom);

    // Print("Try {}.\n", n.ToString());

    CHECK(n > 0);

    // Start with an odd number.
    if (n.IsEven()) {
      n = n + 1;
    }

    while (n < top) {
      if (BigInt::IsProbablyPrime(n, 64)) {
        return n;
      }
      n += 2;
    }
  }
}

// Generates an RSA key pair of a given bit size.
RSAKey GenerateRSAKey(int bits, CryptRand *cr) {
  RSAKey key;

  // Two distinct large prime numbers.
  const int prime_bits = bits / 2;
  key.p = GeneratePrime(prime_bits, cr);
  // key.q = BigInt(7);
  do {
    key.q = GeneratePrime(prime_bits, cr);
  } while (key.p == key.q);

  Print("p: {}\n"
        "q: {}\n",
        key.p.ToString(), key.q.ToString());
  Print("OK...\n");

  // Modulus.
  key.n = key.p * key.q;
  Print("n: {}\n", key.n.ToString());

  // Euler's totient φ(n) = (p - 1) * (q - 1).
  BigInt phi_n = (key.p - 1) * (key.q - 1);

  // Public exponent 'e'. 65537 is conventional; it
  // is fast to multiply because x * 65537 = (x << 16) + x.
  // Must have 1 < e < φ(n) and gcd(e, φ(n)) == 1.
  key.e = BigInt(65537);
  CHECK(BigInt::Eq(BigInt::GCD(key.e, phi_n), 1))
      << "e is not coprime with phi(n). This is very unlikely.";

  // Private exponent 'd', the modular inverse of e mod φ(n).
  {
    std::optional<BigInt> d_opt = BigInt::ModInverse(key.e, phi_n);
    CHECK(d_opt.has_value()) << "e⁻¹ mod φ(n) does not exist";
    key.d = std::move(d_opt.value());
  }

  key.exp1 = BigInt::CMod(key.d, key.p - 1);
  key.exp2 = BigInt::CMod(key.d, key.q - 1);

  {
    std::optional<BigInt> qi_opt = BigInt::ModInverse(key.q, key.p);
    CHECK(qi_opt.has_value()) << "q⁻¹ mod p does not exist";
    key.qinv = std::move(qi_opt.value());
  }

  return key;
}

std::vector<uint8_t> EncodePKCS1(const RSAKey &key) {
  return ASN1::EncodeSequence(ASN1::Concat(
        // Version = RSA.
        ASN1::EncodeInt(BigInt{0}),
        ASN1::EncodeInt(key.n),
        ASN1::EncodeInt(key.e),
        ASN1::EncodeInt(key.d),
        ASN1::EncodeInt(key.p),
        ASN1::EncodeInt(key.q),
        ASN1::EncodeInt(key.exp1),
        ASN1::EncodeInt(key.exp2),
        ASN1::EncodeInt(key.qinv)));
}

std::vector<uint8_t> EncodePKCS8(const RSAKey &key) {
  // OID for rsaEncryption is 1.2.840.113549.1.1.1
  const std::vector<uint8_t> rsa_oid =
    {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01};
  const std::vector<uint8_t> oid_tag = {0x06};

  std::vector<uint8_t> oid_sequence = ASN1::Concat(
      oid_tag,
      ASN1::EncodeLength(rsa_oid.size()), rsa_oid,
      ASN1::EncodeNull());

  return ASN1::EncodeSequence(
      ASN1::Concat(
          // Version = 0.
          ASN1::EncodeInt(BigInt{0}),
          ASN1::EncodeSequence(oid_sequence),
          ASN1::EncodeOctetString(EncodePKCS1(key))));
}



std::string ToPEM(const std::vector<uint8_t> &der_bytes,
                  const std::string &header) {
  std::string b64 = Base64::EncodeV(der_bytes);
  std::string pem = "-----BEGIN " + header + "-----\n";
  for (size_t i = 0; i < b64.length(); i += 64) {
    pem += b64.substr(i, 64) + "\n";
  }
  pem += "-----END " + header + "-----\n";
  return pem;
}


static void Generate() {
  CryptRand cr;
  RSAKey key = GenerateRSAKey(4096, &cr);
  Print("--------\n"
        "n: {}\n"
        "e: {}\n"
        "d: {}\n"
        "p: {}\n"
        "q: {}\n"
        "exp1: {}\n"
        "exp2: {}\n"
        "qinv: {}\n",
        key.n.ToString(),
        key.e.ToString(),
        key.d.ToString(),

        key.p.ToString(),
        key.q.ToString(),
        key.exp1.ToString(),
        key.exp2.ToString(),
        key.qinv.ToString());

  std::string pem = ToPEM(EncodePKCS8(key), "PRIVATE KEY");
  Print("\n\n{}\n", pem);
}

int main(int argc, char **argv) {
  ANSI::Init();

  Generate();

  return 0;
}
