#ifndef _HTTPI_CSR_H
#define _HTTPI_CSR_H

#include <vector>
#include <cstdint>

#include "bignum/big.h"
#include "multi-rsa.h"

struct CSR {

  // Creates a signed Certificate Signing Request as ASN.1 DER.
  static std::vector<uint8_t> Encode(
      // e.g. "tom7.org"
      std::string_view host,
      // e.g. "*.tom7.org", "virus.exe.tom7.org"
      std::span<const std::string> aliases,
      // Need the private key to sign the request.
      const MultiRSA::Key &key);

  // The payload that gets signed.
  static std::vector<uint8_t> CertificationRequestInfo(
      std::string_view host,
      std::span<const std::string> aliases,
      const BigInt &modulus, const BigInt &exponent);

  // Encode an RSA public key as X.509 SubjectPublicKeyInfo.
  // This is part of the above, exposed mainly for testing.
  static std::vector<uint8_t>
  SubjectPublicKeyInfo(const BigInt &modulus, const BigInt &exponent);
};

#endif
