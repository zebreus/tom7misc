
#include <vector>
#include <cstdint>
#include <bit>

#ifdef __cplusplus
extern "C" {
#endif

#include "usoft.h"
#include "unif01.h"
#include "bbattery.h"

#ifdef __cplusplus
}
#endif


#include "util.h"

#include "base/logging.h"

static constexpr int TARGET_FLOATS = 51320000;

using int64 = int64_t;

struct State {
  std::vector<uint8_t> vec;
  int64 idx = 0;

  uint32_t Byte() {
    // If we do wrap around, this is of course not going to be
    // independent from previous bytes! But we assume that no
    // individual test needs more bytes than we have.
    if (idx >= vec.size()) idx = 0;
    return vec[idx++];
  }

  uint32_t Next() {
    uint32_t w = 0;
    w <<= 8; w |= Byte();
    w <<= 8; w |= Byte();
    w <<= 8; w |= Byte();
    w <<= 8; w |= Byte();
    return w;
  }

};


int main(int argc, char **argv) {
  CHECK(argc == 2) << "Needs file on command line.";

  State state;
  state.vec = Util::ReadFileBytes(argv[1]);
  CHECK(state.vec.size() >= TARGET_FLOATS * 4) << "Need "
                                               << (TARGET_FLOATS * 4)
                                               << " bytes. But only had "
                                               << state.vec.size();


  #if 0
  std::vector<float> converted;
  converted.reserve(TARGET_FLOATS);
  for (int i = 0; i < TARGET_FLOATS; i++) {
    uint32_t w = ((uint32_t)vec[i * 4 + 0] << 24) |
      ((uint32_t)vec[i * 4 + 1] << 16) |
      ((uint32_t)vec[i * 4 + 2] << 8) |
      ((uint32_t)vec[i * 4 + 3] << 0);

    float f = (double)w / (double)0x100000000ULL;
    converted.push_back(f);
  }
  #endif

  // We can just use ufile_CreateReadBin maybe? see p86 of
  // testu01.pdf

  unif01_Gen gen;
  gen.state = (void*)&state;
  gen.param = (void*)nullptr;
  gen.name = argv[1];
  gen.GetU01 = +[](void *param, void *state) -> double {
      State *s = (State*)state;
      return (double)s->Next() / (double)0x100000000ULL;
    };

  gen.GetBits = +[](void *param, void *state) -> unsigned long {
      State *s = (State*)state;
      return s->Next();
    };

  gen.Write = +[](void *) {
      printf("(smallcrush-main)");
    };

  bbattery_SmallCrush(&gen);


  {
    int64 bits[2] = {};
    int64 bytes[256] = {};
    for (int i = 0; i < state.vec.size(); i++) {
      uint8_t byte = state.vec[i];
      int b = std::popcount<uint8_t>(byte);
      bits[0] += 8 - b;
      bits[1] += b;
      bytes[byte]++;
    }

    printf("%lld zeroes, %lld ones (%.5f%%)\n",
           bits[0], bits[1], (bits[1] * 100.0) / (bits[0] + bits[1]));
    int mini = 0, maxi = 0;
    for (int i = 1; i < 256; i++) {
      if (bytes[i] < bytes[mini]) mini = i;
      if (bytes[i] > bytes[maxi]) maxi = i;
    }

    printf("Rarest byte 0x%02x, %lld times (%.5f%%)\n"
           "Most common 0x%02x, %lld times (%.5f%%)\n"
           "Exactly 1/256 would be (%.5f%%)\n",
           mini, bytes[mini], (bytes[mini] * 100.0) / state.vec.size(),
           maxi, bytes[maxi], (bytes[maxi] * 100.0) / state.vec.size(),
           100.0 / 256.0);
  }

  printf("OK\n");
  return 0;
}
