
#include "util.h"

#include "crypt/cryptrand.h"

#include "arcfour.h"

// Reference cryptographic random stream, which should
// of course pass the statistical tests.

int main(int argc, char **argv) {

  // Bytes needed for smallcrush.
  static constexpr int SIZE = 205280000;
  std::vector<uint8_t> bytes;
  bytes.reserve(SIZE);


  #if 0
  CryptRand cr;
  for (int i = 0; i < SIZE / 8; i++) {
    if ((i % 4096) == 0) printf("%d/%d\n", i, SIZE / 8);
    uint64_t w = cr.Word64();
    for (int b = 0; b < 8; b++) {
      bytes.push_back(w & 0xFF);
      w >>= 8;
    }
  }
  #endif

  ArcFour rc("okay");
  rc.Discard(1024);
  for (int i = 0; i < SIZE; i++) {
    if (rc.Byte() == 0x01) {
      // deliberate bias
      bytes.push_back(0x7F & rc.Byte());
    } else {
      bytes.push_back(rc.Byte());
    }
  }


  Util::WriteFileBytes("okayrand.bin", bytes);

  printf("Wrote okayrand.bin\n");
  return 0;
}
