
#ifndef _HTTPI_ASN1_H
#define _HTTPI_ASN1_H

#include <span>
#include <vector>
#include <cstdint>
#include <string_view>
#include <cstdlib>

#include "packet-parser.h"
#include "bignum/big.h"

struct ASN1 {
  // Using DER (deterministic) format.

  static std::vector<uint8_t> EncodeLength(size_t length);
  static std::vector<uint8_t> EncodeInt(const BigInt &n);
  static std::vector<uint8_t> EncodeSequence(std::span<const uint8_t> s);
  // Contents must be sorted.
  static std::vector<uint8_t> EncodeSet(std::span<const uint8_t> s);
  static std::vector<uint8_t> EncodeOctetString(std::span<const uint8_t> s);
  static std::vector<uint8_t> EncodeUTF8String(std::string_view s);
  // i.e. ASCII string
  static std::vector<uint8_t> EncodeIA5String(std::string_view s);

  // trailing_unused_bits must be in [0, 7]. The number of bits
  // used is s.size() * 8 - trailing_unused_bits. (The remainder are zeroed.)
  static std::vector<uint8_t> EncodeBitString(std::span<const uint8_t> s,
                                              int trailing_unused_bits);
  static std::vector<uint8_t> EncodeNull();
  static std::vector<uint8_t> EncodeOID(
      const std::vector<uint64_t> &components);
  // "Constructed" tags 0xA0...
  static std::vector<uint8_t> EncodeContextSpecificConstructed(
      uint8_t tag_num, std::span<const uint8_t> content);

  // "Primitive" tags 0x80...
  static std::vector<uint8_t> EncodeContextSpecificPrimitive(
      uint8_t tag_num, std::span<const uint8_t> content);

  enum Tag : uint8_t {
    TAG_BOOLEAN = 0x01,
    TAG_INTEGER = 0x02,
    TAG_BIT_STRING = 0x03,
    TAG_OCTET_STRING = 0x04,
    TAG_NULL = 0x05,
    TAG_OID = 0x06,
    TAG_SEQUENCE = 0x30,
    TAG_SET = 0x31,

    TAG_UTC_TIME = 0x17,
    TAG_GENERALIZED_TIME = 0x18,

    TAG_UTF8_STRING = 0x0C,
    TAG_IA5_STRING = 0x16,

    // Context specific tags are like submessages. They consume
    // the tag on the submessage, but they have their own tag_num
    // bits to indicate which field they are describing (which
    // then implies the type of the encoded message).
    TAG_CONSTRUCTED_0 = 0xA0,
    TAG_PRIMITIVE_0 = 0x80,
  };

  // Like EncodeSequence, but multiple vectors of bytes as arguments;
  // they are just concatenated for convenience (their internal
  // structure is not used/checked).
  template<class ...T>
  static std::vector<uint8_t> EncodeSeq(T &&...vecs);

  template<typename... T>
  static std::vector<uint8_t> Concat(T &&...vecs);

  // Parsing. Parsers put the packet in an error state if they
  // encounter invalid data; so check p->OK().

  // Parses a DER length field.
  static size_t ParseLength(PacketParser *p);

  // Consume a tag-length-value from the packet. Puts the packet in
  // an error state if not the expected tag.
  // Advances p past it, but returns a subpacket to the value portion.
  static PacketParser ParseTLV(PacketParser *p, uint8_t expected_tag);

  // Parse a non-negative integer.
  static BigInt ParseInteger(PacketParser *p);

 private:
  ASN1() = delete;
};


// Template implementations follow.

template<typename... T>
std::vector<uint8_t> ASN1::Concat(T &&...vecs) {
  size_t total_size = (vecs.size() + ... + 0);
  std::vector<uint8_t> result;
  result.reserve(total_size);
  (result.insert(result.end(), vecs.begin(), vecs.end()), ...);
  return result;
}

template<typename... T>
std::vector<uint8_t> ASN1::EncodeSeq(T &&...vecs) {
  return EncodeSequence(Concat(vecs...));
}


#endif
