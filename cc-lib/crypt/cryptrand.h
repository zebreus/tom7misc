
#ifndef _CC_LIB_CRYPT_CRYPTRAND_H
#define _CC_LIB_CRYPT_CRYPTRAND_H

#include <cstdint>
#include <span>

struct CryptRand {
  CryptRand();

  void Bytes(std::span<uint8_t> buffer);

  uint8_t Byte();
  uint64_t Word64();
};

#endif
