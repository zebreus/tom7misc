
#ifndef _HTTPI_ASN1_H
#define _HTTPI_ASN1_H

#include <span>
#include <vector>

struct BigInt;
struct ASN1 {
  // Using DER (deterministic) format.

  static std::vector<uint8_t> EncodeLength(size_t length);
  static std::vector<uint8_t> EncodeInt(const BigInt &n);
  static std::vector<uint8_t> EncodeSequence(std::span<const uint8_t> v);
  static std::vector<uint8_t> EncodeOctetString(std::span<const uint8_t> s);
  // trailing_unused_bits must be in [0, 7]. The number of bits
  // used is s.size() * 8 - trailing_unused_bits. (The remainder are zeroed.)
  static std::vector<uint8_t> EncodeBitString(std::span<const uint8_t> s,
                                              int trailing_unused_bits);
  static std::vector<uint8_t> EncodeNull();
  static std::vector<uint8_t> EncodeOID(
      const std::vector<uint64_t> &components);
  static std::vector<uint8_t> EncodeContextSpecific(
      uint8_t tag_num, std::span<const uint8_t> content);

  enum Tag : uint8_t {
    TAG_INTEGER = 0x02,
    TAG_BIT_STRING = 0x03,
    TAG_OCTET_STRING = 0x04,
    TAG_NULL = 0x05,
    TAG_OID = 0x06,
    TAG_SEQUENCE = 0x30,

    // Context specific tags are like submessages. They consume
    // the tag on the submessage, but they have their own tag_num
    // bits to indicate which field they are describing (which
    // then implies the type of the encoded message).
    TAG_SPECIFIC_0 = 0xA0,
  };

  template<typename... T>
  static std::vector<uint8_t> Concat(T &&...vecs);

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


#endif
