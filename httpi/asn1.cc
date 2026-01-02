
#include "asn1.h"

#include <algorithm>
#include <span>
#include <string_view>
#include <vector>

#include "base/logging.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "packet-parser.h"

inline static void AppendVec(std::vector<uint8_t> *out,
                             std::span<const uint8_t> in) {
  out->insert(out->end(), in.begin(), in.end());
}

inline static void AppendWithLength(std::vector<uint8_t> *out,
                                    std::span<const uint8_t> in) {
  AppendVec(out, ASN1::EncodeLength(in.size()));
  AppendVec(out, in);
}

inline static std::vector<uint8_t> EncodeTLV(
    uint8_t tag,
    std::span<const uint8_t> content) {
  std::vector<uint8_t> result;
  result.reserve(1 + 4 + content.size());
  result.push_back(tag);
  AppendWithLength(&result, content);
  return result;
}

// Encodes a length value in DER format.
std::vector<uint8_t> ASN1::EncodeLength(size_t length) {
  if (length < 128) {
    return {(uint8_t)length};
  } else {
    std::vector<uint8_t> len_bytes;
    while (length > 0) {
      len_bytes.push_back(length & 0xFF);
      length >>= 8;
    }
    // This byte indicates how many bytes follow in the length.
    // Note that the vector is currently reversed.
    len_bytes.push_back(0x80 | len_bytes.size());
    std::reverse(len_bytes.begin(), len_bytes.end());
    return len_bytes;
  }
}

std::vector<uint8_t> ASN1::EncodeInt(const BigInt &n) {
  std::vector<uint8_t> bytes;
  const int sign = BigInt::Sign(n);
  if (sign == 0) {
    bytes.push_back(0);
  } else if (sign == 1) {
    BigInt temp = n;
    while (!BigInt::IsZero(temp)) {
      bytes.push_back((uint8_t)(temp & 0xFF));
      temp >>= 8;
    }

    // DER rules require a single leading byte for positive numbers
    // whose high byte starts with a 1 bit. This distinguishes them from
    // negative numbers. Note that the vector is currently reversed.
    if ((bytes.back() & 0x80) != 0 && BigInt::Sign(n) > 0) {
      bytes.push_back(0x00);
    }

    std::reverse(bytes.begin(), bytes.end());
  } else {
    CHECK(sign == -1);

    LOG(FATAL) << "Unimplemented: Negative integers";
  }

  return EncodeTLV(TAG_INTEGER, bytes);
}

std::vector<uint8_t> ASN1::EncodeSequence(
    std::span<const uint8_t> content) {
  return EncodeTLV(TAG_SEQUENCE, content);
}

std::vector<uint8_t> ASN1::EncodeSet(
    std::span<const uint8_t> content) {
  return EncodeTLV(TAG_SET, content);
}

std::vector<uint8_t> ASN1::EncodeOctetString(
    std::span<const uint8_t> content) {
  return EncodeTLV(TAG_OCTET_STRING, content);
}

static std::span<const uint8_t> AsBytes(std::string_view s) {
  return std::span<const uint8_t>((const uint8_t*)s.data(), s.size());
}

std::vector<uint8_t> ASN1::EncodeUTF8String(std::string_view s) {
  return EncodeTLV(TAG_UTF8_STRING, AsBytes(s));
}

std::vector<uint8_t> ASN1::EncodeIA5String(std::string_view s) {
  return EncodeTLV(TAG_IA5_STRING, AsBytes(s));
}

std::vector<uint8_t> ASN1::EncodeBitString(
    std::span<const uint8_t> content,
    int trailing_unused_bits) {
  CHECK(trailing_unused_bits >= 0 &&
        trailing_unused_bits < 8) << trailing_unused_bits;
  CHECK(trailing_unused_bits == 0 || !content.empty());

  std::vector<uint8_t> result;
  result.reserve(1 + 4 + 1 + content.size());
  result.push_back(TAG_BIT_STRING);
  AppendVec(&result, ASN1::EncodeLength(1 + content.size()));
  result.push_back((uint8_t)trailing_unused_bits);

  for (int i = 0; i < content.size(); i++) {
    uint8_t b = content[i];
    if (i == content.size() - 1) {
      b >>= trailing_unused_bits;
      b <<= trailing_unused_bits;
    }
    result.push_back(b);
  }

  return result;
}

std::vector<uint8_t> ASN1::EncodeNull() {
  return std::vector<uint8_t>({ASN1::TAG_NULL, 0x00});
}

std::vector<uint8_t> ASN1::EncodeOID(const std::vector<uint64_t> &components) {
  CHECK(components.size() >= 2);
  CHECK(components[0] <= 2) << "First OID component must be 0, 1, or 2.";
  CHECK(components[0] < 2 ? components[1] < 40 : true)
      << "Second OID component is out of range for ITU-T or ISO formats. "
      << components[0];

  std::vector<uint8_t> content;
  content.reserve(components.size() * 2);
  content.push_back(components[0] * 40 + components[1]);

  for (size_t i = 2; i < components.size(); i++) {
    uint64_t val = components[i];
    if (val == 0) {
      content.push_back(0);
      continue;
    }

    std::vector<uint8_t> component_bytes;
    while (val > 0) {
      component_bytes.push_back(val & 0x7F);
      val >>= 7;
    }
    std::reverse(component_bytes.begin(), component_bytes.end());

    // Set continuation bit on all but the last byte.
    for (size_t j = 0; j < component_bytes.size() - 1; j++) {
      component_bytes[j] |= 0x80;
    }
    AppendVec(&content, component_bytes);
  }

  return EncodeTLV(TAG_OID, content);
}

std::vector<uint8_t> ASN1::EncodeContextSpecificConstructed(
    uint8_t tag_num, std::span<const uint8_t> content) {
  CHECK(tag_num < 0x1F) << "Context specific tags can only use 5 "
    "bits with this encoding, and cannot be all 1 bits.";
  const uint8_t tag_byte = 0xA0 | tag_num;
  return EncodeTLV(tag_byte, content);
}

std::vector<uint8_t> ASN1::EncodeContextSpecificPrimitive(
    uint8_t tag_num, std::span<const uint8_t> content) {
  CHECK(tag_num < 0x1F) << "Context specific tags can only use 5 "
    "bits with this encoding, and cannot be all 1 bits.";
  const uint8_t tag_byte = 0x80 | tag_num;
  return EncodeTLV(tag_byte, content);
}


size_t ASN1::ParseLength(PacketParser *p) {
  uint8_t len_byte = p->Byte();
  if ((len_byte & 0x80) == 0) {
    return len_byte;
  } else {
    const int num_bytes = len_byte & 0x7F;
    if (num_bytes < 0 || num_bytes > 4) {
      p->Error();
      return 0;
    }
    size_t length = 0;
    for (int i = 0; i < num_bytes; i++) {
      length = (length << 8) | p->Byte();
    }
    return length;
  }
}

PacketParser ASN1::ParseTLV(PacketParser *p, uint8_t expected_tag) {
  const uint8_t tag = p->Byte();
  if (tag != expected_tag) p->Error();
  const size_t len = ParseLength(p);
  return p->Subpacket(len);
}

BigInt ASN1::ParseInteger(PacketParser *p) {
  return BigInt::FromBigEndianBytes(ParseTLV(p, ASN1::TAG_INTEGER).View());
}
