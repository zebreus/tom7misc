#include "csr.h"

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asn1.h"
#include "base/print.h"
#include "bignum/big.h"
#include "crypt/sha256.h"
#include "multi-rsa.h"
#include "packet-parser.h"
#include "util.h"

static constexpr bool VERBOSE = false;

static std::vector<uint8_t> ConcatV(
    const std::vector<std::vector<uint8_t>> &parts) {
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


  // An extensions sequence containing one extension:
  // subjectAltName (2.5.29.17)
  std::vector<uint8_t> ext =
    ASN1::EncodeSequence(
        ASN1::EncodeSeq(ASN1::EncodeOID({2, 5, 29, 17}),
                        ASN1::EncodeOctetString(sans)));

  // More boilerplate to make the extensions list into an attribute.
  // Note that the set of extensions needs to be sorted, but there is
  // just one.
  std::vector<uint8_t> attr =
    ASN1::EncodeSeq(ASN1::EncodeOID({1, 2, 840, 113549, 1, 9, 14}),
                    ASN1::EncodeSet(ext));

  // Then wrapped into a set of attributes. More attributes could be
  // added here, sorting by their DER bytes. The set tag is implicit
  // (context-specific).
  std::vector<uint8_t> attr_set =
    ASN1::EncodeContextSpecificConstructed(0, attr);

  // CertificationRequestInfo
  return ASN1::EncodeSeq(version, subject, spki, attr_set);
}

// Consume one of the two time types next in the stream.
static PacketParser ConsumeTime(PacketParser *p) {
  uint8_t tag = (*p)[0];
  if (tag != ASN1::TAG_UTC_TIME &&
      tag != ASN1::TAG_GENERALIZED_TIME) {
    p->Error();
    return *p;
  }
  return ASN1::ParseTLV(p, tag);
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

  // Skip notBefore.
  [[maybe_unused]] PacketParser not_before = ConsumeTime(&validity);

  // notAfter is the expiration time we're looking for.
  PacketParser not_after = ConsumeTime(&validity);

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

std::optional<std::pair<BigInt, BigInt>>
CSR::ParseSubjectPublicKeyInfo(std::span<const uint8_t> spki_der) {
  PacketParser p(spki_der);

  // Unwrap the outer SubjectPublicKeyInfo Sequence
  PacketParser spki = ASN1::ParseTLV(&p, ASN1::TAG_SEQUENCE);
  if (!spki.OK() || !p.empty()) return std::nullopt;

  // Only RSA keys are supported (1.2.840.113549.1.1.1).
  static constexpr uint8_t RSA_OID[] =
    { 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01 };
  PacketParser algo = ASN1::ParseTLV(&spki, ASN1::TAG_SEQUENCE);

  if (!algo.TryStripPrefix(RSA_OID)) {
    return std::nullopt;
  }

  PacketParser bits = ASN1::ParseTLV(&spki, ASN1::TAG_BIT_STRING);
  // Should be no padding for keys.
  if (bits.Byte() != 0) return std::nullopt;

  // Unwrap the inner PKCS#1 RSAPublicKey Sequence
  PacketParser rsa_key = ASN1::ParseTLV(&bits, ASN1::TAG_SEQUENCE);
  BigInt n = ASN1::ParseInteger(&rsa_key);
  BigInt e = ASN1::ParseInteger(&rsa_key);

  if (!rsa_key.OK() || BigInt::Sign(n) != 1 || BigInt::Sign(e) != 1)
    return std::nullopt;

  return std::make_optional(std::make_pair(std::move(n), std::move(e)));
}

std::optional<std::pair<BigInt, BigInt>>
CSR::GetPublicKey(std::span<const uint8_t> cert_der) {

  PacketParser p(cert_der);
  PacketParser cert = ASN1::ParseTLV(&p, ASN1::TAG_SEQUENCE);
  PacketParser tbs = ASN1::ParseTLV(&cert, ASN1::TAG_SEQUENCE);

  if (!tbs.OK()) return std::nullopt;

  // Skip optional version.
  if (!tbs.empty() && (tbs[0] & 0xF0) == 0xA0) {
    (void)ASN1::ParseTLV(&tbs, 0xA0);
  }

  // serialNumber, signature, issuer, validity, subject
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_INTEGER);
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);

  // Find the span that is the SubjectPublicKeyInfo and parse that.
  std::span<const uint8_t> rest = tbs.View();

  PacketParser spki_val = ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);
  if (!spki_val.OK()) return std::nullopt;

  size_t bytes_consumed = rest.size() - tbs.size();
  std::span<const uint8_t> spki = rest.subspan(0, bytes_consumed);

  return ParseSubjectPublicKeyInfo(spki);
}

std::vector<uint8_t> CSR::GetSerialNumber(std::span<const uint8_t> cert_der) {
  PacketParser p(cert_der);

  PacketParser cert = ASN1::ParseTLV(&p, ASN1::TAG_SEQUENCE);
  PacketParser tbs = ASN1::ParseTLV(&cert, ASN1::TAG_SEQUENCE);

  if (!tbs.empty() && (tbs[0] & 0xF0) == 0xA0) {
    (void)ASN1::ParseTLV(&tbs, 0xA0);
  }

  // The serial number.
  PacketParser ss = ASN1::ParseTLV(&tbs, ASN1::TAG_INTEGER);
  if (!ss.OK()) {
    return {};
  }

  // Extract the raw bytes of the integer.
  // Note: Since this is a DER INTEGER, if the MSB is 1, it will include
  // a leading 0x00 byte to indicate it is positive. This copies the exact
  // payload of the TLV.
  std::vector<uint8_t> serial;
  serial.reserve(ss.size());
  for (size_t i = 0; i < ss.size(); i++) {
    serial.push_back(ss[i]);
  }

  return serial;
}


std::vector<std::string> CSR::GetCRLUrls(std::span<const uint8_t> cert_der) {
  PacketParser p(cert_der);

  PacketParser cert = ASN1::ParseTLV(&p, ASN1::TAG_SEQUENCE);
  PacketParser tbs = ASN1::ParseTLV(&cert, ASN1::TAG_SEQUENCE);

  // Skip version.
  if (!tbs.empty() && (tbs[0] & 0xF0) == 0xA0) {
    (void)ASN1::ParseTLV(&tbs, 0xA0);
  }

  // Skip serial, signature algorithm, issuer, validity, subject,
  // SPKI
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_INTEGER);
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);

  // Skip Optional Issuer / Subject unique ids.
  if (!tbs.empty() && (tbs[0] & 0x1F) == 1) (void)ASN1::ParseTLV(&tbs, tbs[0]);
  if (!tbs.empty() && (tbs[0] & 0x1F) == 2) (void)ASN1::ParseTLV(&tbs, tbs[0]);

  // Are there extensions?
  if (!tbs.HasPrefixByte(0xA3)) {
    return {};
  }

  PacketParser extensions_wrapper = ASN1::ParseTLV(&tbs, 0xA3);
  PacketParser extensions =
    ASN1::ParseTLV(&extensions_wrapper, ASN1::TAG_SEQUENCE);

  // The CRL Distribution Points extension is a sequence
  // starting with OID 2.5.29.31, which is encoded in DER as follows:
  static constexpr uint8_t CRL_OID[] =
    { 0x06, 0x03, 0x55, 0x1D, 0x1F };

  std::vector<std::string> urls;

  while (!extensions.empty()) {
    if (!extensions.OK()) {
      if (VERBOSE) Print("extensions not ok\n");
      return {};
    }
    PacketParser ext = ASN1::ParseTLV(&extensions, ASN1::TAG_SEQUENCE);

    // Find only the CRL extension.
    if (!ext.TryStripPrefix(CRL_OID)) {
      continue;
    }

    // Optional "critical" tag we don't care about
    if (ext.HasPrefixByte(ASN1::TAG_BOOLEAN)) {
      (void)ASN1::ParseTLV(&ext, ASN1::TAG_BOOLEAN);
    }

    // Embedded message with a sequence of distribution points.
    PacketParser message = ASN1::ParseTLV(&ext, ASN1::TAG_OCTET_STRING);
    PacketParser dp_seq = ASN1::ParseTLV(&message, ASN1::TAG_SEQUENCE);

    while (!dp_seq.empty() && dp_seq.OK()) {
      if (!dp_seq.OK()) {
        if (VERBOSE) Print("dp seq not ok\n");
        return {};
      }
      PacketParser dp = ASN1::ParseTLV(&dp_seq, ASN1::TAG_SEQUENCE);

      // This is again structured message: A sequence
      // with distributionPoint, reasons, cRLIssuer. We want the
      // distribution point (tag 0).
      if (dp.HasPrefixByte(ASN1::TAG_CONSTRUCTED_0 | 0x00)) {
        PacketParser name_seq =
          ASN1::ParseTLV(&dp, ASN1::TAG_CONSTRUCTED_0 | 0x00);

        // ... and the fullName within that (tag 0).
        if (name_seq.HasPrefixByte(ASN1::TAG_CONSTRUCTED_0 | 0x00)) {
           PacketParser general_names =
             ASN1::ParseTLV(&name_seq, ASN1::TAG_CONSTRUCTED_0 | 0x00);

           while (!general_names.empty()) {
             if (!general_names.OK()) {
               if (VERBOSE) Print("general_names not ok\n");
               return {};
             }
             uint8_t tag = general_names[0];
             PacketParser name = ASN1::ParseTLV(&general_names, tag);

             if (tag == (ASN1::TAG_PRIMITIVE_0 | 0x06)) {
               urls.push_back(name.String());
             }
           }
        }
      }
    }

    // We're done once we've found the CRL extension.
    break;
  }

  return urls;
}

bool CSR::IsRevoked(std::span<const uint8_t> crl_der,
                    std::span<const uint8_t> serial) {

  PacketParser p(crl_der);
  PacketParser cert_list = ASN1::ParseTLV(&p, ASN1::TAG_SEQUENCE);
  PacketParser tbs = ASN1::ParseTLV(&cert_list, ASN1::TAG_SEQUENCE);

  // Skip optional version.
  if (tbs.HasPrefixByte(ASN1::TAG_INTEGER)) {
    (void)ASN1::ParseTLV(&tbs, ASN1::TAG_INTEGER);
  }

  // Skip Signature, Issuer, thisUpdate, nextUpdate.
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);
  (void)ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);
  [[maybe_unused]] PacketParser this_update = ConsumeTime(&tbs);

  if (tbs.HasPrefixByte(ASN1::TAG_UTC_TIME) ||
      tbs.HasPrefixByte(ASN1::TAG_GENERALIZED_TIME)) {
    [[maybe_unused]] PacketParser next_update = ConsumeTime(&tbs);
  }

  // revokedCertificates is an optional sequence of sequences.
  if (!tbs.HasPrefixByte(ASN1::TAG_SEQUENCE)) {
    // Nothing is revoked.
    return false;
  }

  PacketParser revoked_list = ASN1::ParseTLV(&tbs, ASN1::TAG_SEQUENCE);

  while (!revoked_list.empty()) {
    if (!revoked_list.OK()) {
      return false;
    }

    PacketParser entry = ASN1::ParseTLV(&revoked_list, ASN1::TAG_SEQUENCE);
    PacketParser revoked_serial = ASN1::ParseTLV(&entry, ASN1::TAG_INTEGER);

    std::span<const uint8_t> v = revoked_serial.View();
    if (VERBOSE) {
      Print("Revoked:");
      for (uint8_t x : v) {
        Print(" {:02x}", x);
      }
      Print("\n");
    }

    if (revoked_serial.Equals(serial)) {
      return true;
    }
  }

  return false;
}
