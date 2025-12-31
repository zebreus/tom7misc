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

  // For certificate renewal you often need to check the
  // expiration time of a certificate. Pass the ASN.1 DER bytes.
  // Returns 0 if the expiration cannot be parsed (so the certificate
  // is probably invalid).
  static time_t GetExpirationTime(std::span<const uint8_t> cert_der);
  // Or empty string on failure.
  static std::string GetExpirationTimeString(
      std::span<const uint8_t> cert_der);

  // Parse UTC Time (260221173112Z) or Generalized Time (20500221173112Z)
  // as used in X.509.
  static std::optional<time_t> ParseExpirationTime(std::string_view t);
};

#endif
