
#include <algorithm>
#include <cstdio>
#include <memory>
#include <cstdint>
#include <vector>

#include "image.h"
#include "base/logging.h"
#include "base/stringprintf.h"

static constexpr int NUM_SHOW = 11;
int main(int argc, char **argv) {
  std::unique_ptr<ImageRGBA> database(ImageRGBA::Load("database.png"));
  CHECK(database.get() != nullptr);

  for (int addr = 0; addr < 2048; addr++) {
    std::vector<int> ok, fail;
    for (int val = 0; val < 256; val++) {
      uint32_t c = database->GetPixel32(addr, val);
      CHECK(c != 0x000000FF) <<
        StringPrintf("Incomplete at %04x = %02x\n", addr, val);


      bool is_ok = c != 0xFF0000FF;
      if (is_ok) ok.push_back(val);
      else fail.push_back(val);
    }

    if (fail.size() > 0) {
      printf("%04x: %.2f%% (%d fail)", addr,
             (fail.size() * 100.0) / 256.0, (int)fail.size());

      if (ok.size() < NUM_SHOW) {
        printf(" must be:");
        for (int i = 0; i < std::min((int)ok.size(), NUM_SHOW); i++)
          printf(" %02x", ok[i]);
        if (ok.size() > NUM_SHOW) printf(" + %d...",
                                         (int)ok.size() - NUM_SHOW);
        printf("\n");
      } else {
        printf(" can't be:");
        for (int i = 0; i < std::min((int)fail.size(), NUM_SHOW); i++)
          printf(" %02x", fail[i]);
        if (fail.size() > NUM_SHOW) printf(" + %d...",
                                           (int)fail.size() - NUM_SHOW);
        printf("\n");
      }
    }
  }

  return 0;
}
