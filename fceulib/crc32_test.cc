
#include "utils/crc32.h"

#include <string>
#include <cstdint>
#include <cstdio>

#include "base/logging.h"
#include "base/stringprintf.h"

#define CHECK_CRC(e1, e2) do {                                        \
    uint32_t crc = (e1);                                              \
    CHECK_EQ(crc, e2) << #e1 << ": " << StringPrintf("0x%08x", crc);  \
  } while (0)

static uint32_t CRCString(uint32_t init,
                          const std::string &s) {
  return CalcCRC32(init, (const uint8_t *)s.data(), s.size());
}

static void TestKnown() {
  CHECK_CRC(CRCString(0, "nintendo does what nintendoes"), 0x09a951c4);
  CHECK_CRC(CRCString(0xCAFED00D, ""), 0xCAFED00D);
  CHECK_CRC(CRCString(0xCAFED00D, "_"), 0x57ad2185);
  std::string zeroes = "100000";
  zeroes[2] = '\0';
  zeroes[3] = '\0';
  CHECK_CRC(CRCString(0xDD, zeroes), 0x88fac483);
  const char *lorem =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
    "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut "
    "enim ad minim veniam, quis nostrud exercitation ullamco laboris "
    "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor "
    "in reprehenderit in voluptate velit esse cillum dolore eu fugiat "
    "nulla pariatur. Excepteur sint occaecat cupidatat non proident, "
    "sunt in culpa qui officia deserunt mollit anim id est laborum.";
  CHECK_CRC(CRCString(0, lorem), 0x98b2c5bd);
}

int main(int argc, char **argv) {

  TestKnown();

  printf("OK\n");
  return 0;
}
