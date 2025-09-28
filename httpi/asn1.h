
#ifndef _HTTPI_ASN1_H
#define _HTTPI_ASN1_H

#include <vector>

struct BigInt;
struct ASN1 {

  // Using DER (deterministic) format.

  static std::vector<uint8_t> EncodeLength(size_t length);
  static std::vector<uint8_t> EncodeInt(const BigInt &n);
  static std::vector<uint8_t> EncodeSequence(const std::vector<uint8_t> &v);
  static std::vector<uint8_t> EncodeOctetString(const std::vector<uint8_t> &s);
  static std::vector<uint8_t> EncodeNull();
  static std::vector<uint8_t> EncodeOID(const std::vector<uint64_t> &components);

  enum Tag : uint8_t {
    TAG_INTEGER = 0x02,
    TAG_OCTET_STRING = 0x04,
    TAG_NULL = 0x05,
    TAG_OID = 0x06,
    TAG_SEQUENCE = 0x30,
  };

  template<typename... T>
  static std::vector<uint8_t> Concat(T&&... vecs);

 private:
  ASN1() = delete;
};


// Template implementations follow.

template<typename... T>
std::vector<uint8_t> ASN1::Concat(T&&... vecs) {
  size_t total_size = (vecs.size() + ... + 0);
  std::vector<uint8_t> result;
  result.reserve(total_size);
  (result.insert(result.end(), vecs.begin(), vecs.end()), ...);
  return result;
}


#endif
