
#include "montgomery64.h"
#include <cstdint>

#include "base/logging.h"
#include "base/int128.h"
#include "ansi.h"

static inline uint64_t PlusMod(uint64_t a, uint64_t b, uint64_t m) {
  uint128_t aa(a);
  uint128_t bb(b);
  uint128_t mm(m);
  uint128_t rr = (aa + bb) % mm;
  CHECK(Uint128High64(rr) == 0);
  return Uint128Low64(rr);
}

static inline uint64_t SubMod(uint64_t a, uint64_t b, uint64_t m) {
  int128_t aa(a);
  int128_t bb(b);
  int128_t mm(m);
  int128_t rr = (aa - bb) % mm;
  if (rr < 0) {
    rr += m;
  }
  CHECK(rr >= 0);
  uint128_t uu = (uint128_t)rr;
  CHECK(Uint128High64(uu) == 0);
  uint64_t u = (uint64_t)Uint128Low64(uu);
  // printf("%llu - %llu mod %llu = %llu\n", a, b, m, u);
  return u;
}

static inline uint64_t MultMod(uint64_t a, uint64_t b,
                               uint64_t m) {
  uint128 aa(a);
  uint128 bb(b);
  uint128 mm(m);

  uint128 rr = (aa * bb) % mm;
  CHECK(Uint128High64(rr) == 0);
  return Uint128Low64(rr);
}


static void TestBasic() {
  for (uint64_t m : std::initializer_list<uint64_t>{
      7, 11, 21, 19, 121, 65, 31337, 131073, 999998999,
      10000001000001,
      // 2^62 -1, +1,
      4611686018427387903, 4611686018427387905,
    }) {
    CHECK((m & 1) == 1) << "Modulus must be odd: " << m;

    const MontgomeryRep64 rep(m);

    for (uint64_t a : std::initializer_list<uint64_t>{
        0, 1, 2, 3, 4, 5, 7, 12, 120, 64, 31337, 131072, 999999999,
        10000000000001,
        // 2^62 -1, +0, +1
        4611686018427387903, 4611686018427387904, 4611686018427387905,
      }) {

      const uint64_t amod = a % m;
      const Montgomery64 am = rep.ToMontgomery(amod);
      CHECK(rep.ToInt(am) == amod) << a << " mod " << m;

      for (uint64_t b : std::initializer_list<uint64_t>{
          0, 1, 2, 7, 11, 16, 19, 120, 64, 31337, 131072, 999998999,
          10000001000001,
          // 2^62 -1, +0, +1
          4611686018427387903, 4611686018427387904, 4611686018427387905,
        }) {

        const uint64_t bmod = b % m;
        const Montgomery64 bm = rep.ToMontgomery(bmod);
        CHECK(rep.ToInt(bm) == bmod) << b << " mod " << m;

        // Plus
        const Montgomery64 aplusb = rep.Add(am, bm);
        CHECK(rep.ToInt(aplusb) == PlusMod(amod, bmod, m));

        const Montgomery64 aminusb = rep.Sub(am, bm);
        CHECK(rep.ToInt(aminusb) == SubMod(amod, bmod, m)) <<
          amod << " - " << bmod << " mod " << m;

        const Montgomery64 bminusa = rep.Sub(bm, am);
        CHECK(rep.ToInt(bminusa) == SubMod(bmod, amod, m));

        const Montgomery64 atimesb = rep.Mult(am, bm);
        CHECK(rep.ToInt(atimesb) == MultMod(amod, bmod, m));

      }
    }
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestBasic();

  printf("OK");
  return 0;
}
