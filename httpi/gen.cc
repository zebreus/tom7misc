
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "bignum/big.h"
#include "crypt/cryptrand.h"

#include "asn1.h"
#include "pem.h"
#include "rsa.h"

std::vector<uint8_t> EncodePKCS1(const RSA::Key &key) {
  return ASN1::EncodeSequence(ASN1::Concat(
        // Version 0 = RSA.
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

std::vector<uint8_t> EncodePKCS8(const RSA::Key &key) {
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

static RSA::Key GenerateBad(int bits, CryptRand *cr) {
  BigInt q(31337);
  const int prime_bits = bits - BigInt::NumBits(q);
  for (;;) {
    // Two distinct large prime numbers.
    BigInt p = RSA::GeneratePrime(prime_bits, cr);

    auto ko = RSA::KeyFromPrimes(std::move(p), q);
    if (ko.has_value()) {
      if (BigInt::NumBits(ko.value().n) == bits) {
        return std::move(ko.value());
      } else {
        Print("Not enough bits.\n");
      }
    }
  }
}


static void Generate() {
  CryptRand cr;
  // RSA::Key key = RSA::GenerateKey(4096, &cr);
  RSA::Key key = GenerateBad(4096, &cr);


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

  Print("n bits: {}\n", BigInt::NumBits(key.n));

  std::string pem = PEM::ToPEM(EncodePKCS8(key), "PRIVATE KEY");
  Print("\n\n{}\n", pem);
}

int main(int argc, char **argv) {
  ANSI::Init();

  Generate();

  return 0;
}
