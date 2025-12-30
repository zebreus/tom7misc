#include "csr.h"

#include <cstdint>
#include <vector>
#include <string>

#include "asn1.h"
#include "bignum/big.h"
#include "vector-util.h"
#include "crypt/sha256.h"

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
    ASN1::EncodeSequence(VectorConcat(
                             ASN1::EncodeOID({1, 2, 840, 113549, 1, 1, 1}),
                             ASN1::EncodeNull()));

  const std::vector<uint8_t> key =
    ASN1::EncodeBitString(
        ASN1::EncodeSequence(VectorConcat(
                                 ASN1::EncodeInt(modulus),
                                 ASN1::EncodeInt(exponent))),
        0);

  return ASN1::EncodeSequence(VectorConcat(algo_id, key));
}

std::vector<uint8_t> CSR::CertificationRequestInfo(
    std::string_view host,
    std::span<const std::string> aliases,
    const BigInt &modulus, const BigInt &exponent) {

  std::vector<uint8_t> spki = SubjectPublicKeyInfo(modulus, exponent);

  std::vector<uint8_t> version = ASN1::EncodeInt(BigInt(0));

  std::vector<uint8_t> subject = ASN1::EncodeSequence(
      ASN1::EncodeSet(
          ASN1::EncodeSequence(
              VectorConcat(ASN1::EncodeOID({2, 5, 4, 3}),
                           ASN1::EncodeUTF8String(host)))));

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
        ASN1::EncodeSequence(VectorConcat(ASN1::EncodeOID({2, 5, 29, 17}),
                                          ASN1::EncodeOctetString(sans))));

  // More boilerplate to make the extensions list into an attribute.
  // Note that the set of extensions needs to be sorted, but there is just one.
  std::vector<uint8_t> attr =
    ASN1::EncodeSequence(VectorConcat(ASN1::EncodeOID({1, 2, 840, 113549, 1, 9, 14}),
                                      ASN1::EncodeSet(ext)));

  // 5. Context Specific Tag for the Attributes field in the CSR
  // attributes [0] IMPLICIT Attributes
  // The 'Attributes' type is a SET OF Attribute.
  // However, older PKCS#10 definitions sometimes treated this loosely.
  // Technically, we need to encode a SET of Attributes, then apply Context Tag [0].
  // But because it is IMPLICIT, the SET tag is replaced by [0] (Constructed).
  // So we take our Attribute Sequence(s), put them in a buffer, and wrap that buffer
  // with Tag [0].

  // Then wrapped into a set of attributes. More attributes could be added here,
  // sorting by their DER bytes. The set tag is implicit (context-specific).
  std::vector<uint8_t> attr_set = ASN1::EncodeContextSpecificConstructed(0, attr);

  // CertificationRequestInfo
  return ASN1::EncodeSequence(
      VectorConcat(
        version,
        subject,
        spki,
        attr_set));
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
