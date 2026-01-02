#include "csr.h"

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asn1.h"
#include "bignum/big.h"
#include "crypt/sha256.h"
#include "multi-rsa.h"
#include "packet-parser.h"
#include "util.h"

static std::vector<uint8_t> ConcatV(const std::vector<std::vector<uint8_t>> &parts) {
  std::vector<uint8_t> out;
  for (const std::vector<uint8_t> &v : parts) {
    out.insert(out.end(), v.begin(), v.end());
  }
  return out;
}

std::vector<uint8_t> CSR::SubjectPublicKeyInfo(const BigInt &modulus,
                                               const BigInt &exponent) {
  // Algorithm is RSA (1.2.840.113549.1.1.1) with no params.
  const std::vector<uint8_t> algo_id =
    ASN1::EncodeSeq(ASN1::EncodeOID({1, 2, 840, 113549, 1, 1, 1}),
                    ASN1::EncodeNull());

  const std::vector<uint8_t> key =
    ASN1::EncodeBitString(
        ASN1::EncodeSeq(ASN1::EncodeInt(modulus),
                        ASN1::EncodeInt(exponent)),
        0);

  return ASN1::EncodeSeq(algo_id, key);
}

std::vector<uint8_t> CSR::CertificationRequestInfo(
    std::string_view host,
    std::span<const std::string> aliases,
    const BigInt &modulus, const BigInt &exponent) {

  std::vector<uint8_t> spki = SubjectPublicKeyInfo(modulus, exponent);

  std::vector<uint8_t> version = ASN1::EncodeInt(BigInt(0));

  std::vector<uint8_t> subject = ASN1::EncodeSequence(
      ASN1::EncodeSet(
          ASN1::EncodeSeq(ASN1::EncodeOID({2, 5, 4, 3}),
                          ASN1::EncodeUTF8String(host))));

  // The SubjectAlternativeName extension is required for Let's Encrypt.

  std::vector<std::vector<uint8_t>> names;
  for (const std::string &alias : aliases) {
    // An ASCII string, but using an implicit tag.
    std::span<const uint8_t> ascii((const uint8_t *)alias.data(), alias.size());
    names.push_back(ASN1::EncodeContextSpecificPrimitive(2, ascii));
  }
  std::vector<uint8_t> sans = ASN1::EncodeSequence(ConcatV(names));


  // An extensions sequence containing one extension: subjectAltName (2.5.29.17)
  std::vector<uint8_t> ext =
    ASN1::EncodeSequence(
        ASN1::EncodeSeq(ASN1::EncodeOID({2, 5, 29, 17}),
                        ASN1::EncodeOctetString(sans)));

  // More boilerplate to make the extensions list into an attribute.
  // Note that the set of extensions needs to be sorted, but there is just one.
  std::vector<uint8_t> attr =
    ASN1::EncodeSeq(ASN1::EncodeOID({1, 2, 840, 113549, 1, 9, 14}),
                    ASN1::EncodeSet(ext));

  // Then wrapped into a set of attributes. More attributes could be added here,
  // sorting by their DER bytes. The set tag is implicit (context-specific).
  std::vector<uint8_t> attr_set = ASN1::EncodeContextSpecificConstructed(0, attr);

  // CertificationRequestInfo
  return ASN1::EncodeSeq(version, subject, spki, attr_set);
}

std::vector<uint8_t> CSR::Encode(
    // e.g. "tom7.org"
    std::string_view host,
    // e.g. "*.tom7.org", "virus.exe.tom7.org"
    std::span<const std::string> aliases,
    // Need the private key to sign the request.
    const MultiRSA::Key &key) {

  std::vector<uint8_t> info =
    CertificationRequestInfo(host, aliases, key.n, key.e);
  std::vector<uint8_t> hash = SHA256::HashVector(info);
  std::vector<uint8_t> sig = MultiRSA::SignSHA256(key, hash);

  return ASN1::EncodeSeq(
    info,
    // sha256WithRSAEncryption
    ASN1::EncodeSeq(
        ASN1::EncodeOID({1, 2, 840, 113549, 1, 1, 11}),
        ASN1::EncodeNull()),
    ASN1::EncodeBitString(sig, 0));

}

std::string CSR::GetExpirationTimeString(std::span<const uint8_t> cert_der) {
  PacketParser p(cert_der);

  PacketParser cert = ASN1::ParseTLV(&p, ASN1::TAG_SEQUENCE);
  PacketParser tbs = ASN1::ParseTLV(&cert, ASN1::TAG_SEQUENCE);

  // Skip Version if it's there.
  // Version is context-specific [0]. If present, skip it.
  // Note: We peek at the byte. 0xA0 is [0] Constructed.
  if (!tbs.empty() && (tbs[0] & 0xF0) == 0xA0) {
    (void)ASN1::ParseTLV(&tbs, 0xA0);
  }

  // Serial.
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_INTEGER);
  // Signature algorithm.
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);

  // Issuer.
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);

  PacketParser validity = ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);

  // Skip notBefore, which is one of two time types.
  uint8_t tag_nb = validity[0];
  if (tag_nb != ASN1::TAG_UTC_TIME &&
      tag_nb != ASN1::TAG_GENERALIZED_TIME) p.Error();
  (void)ASN1::ParseTLV(&validity, tag_nb);

  // notAfter is the expiration time we're looking for.
  uint8_t tag_na = validity[0];
  if (tag_na != ASN1::TAG_UTC_TIME &&
      tag_na != ASN1::TAG_GENERALIZED_TIME) p.Error();

  PacketParser not_after = ASN1::ParseTLV(&validity, tag_na);
  std::string ret = not_after.String();
  // Make sure everything was OK. The subpacket inherits the error
  // state from its parent(s).
  if (!not_after.OK())
    return "";

  return ret;
}

// Only two formats are allowed by X.509.
// We are permissive about the YYYY format being used for dates before 2050.
std::optional<time_t> CSR::ParseExpirationTime(std::string_view t) {
  //   260221173112Z
  // 20500221173112Z
  PacketParser p(t);
  int year = 0;
  if (p.size() == 15) {
    year = atoi(p.String(4).c_str());
  } else if (p.size() == 13) {
    int yy = atoi(p.String(2).c_str());
    year = (yy >= 50) ? 1900 + yy : 2000 + yy;
  } else {
    return std::nullopt;
  }

  int month = atoi(p.String(2).c_str());
  int day = atoi(p.String(2).c_str());
  int hh = atoi(p.String(2).c_str());
  int mm = atoi(p.String(2).c_str());
  int ss = atoi(p.String(2).c_str());
  uint8_t z = p.Byte();
  if (z != 'Z' || !p.OK())
    return std::nullopt;

  return std::make_optional(
      (time_t)Util::UnixTime(year, month, day, hh, mm, ss));
}

time_t CSR::GetExpirationTime(std::span<const uint8_t> cert_der) {
  return ParseExpirationTime(
      GetExpirationTimeString(cert_der)).value_or(0);
}
