
#include <string>
#include <cmath>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "util.h"
#include "image.h"
#include "bounds.h"
#include "opt/optimizer.h"
#include "half.h"
#include "color-util.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"

#include "grad-util.h"
#include "expression.h"
#include "choppy.h"
#include "ansi.h"
#include "bitbuffer.h"
#include "timer.h"

using namespace std;

using namespace half_float::literal;

using Allocator = Exp::Allocator;
using DB = Choppy::DB;
using Table = GradUtil::Table;
using Step = GradUtil::Step;
using State = GradUtil::State;



static const uint8_t PERMS[16][16] = {
  // length 5 cycle, length 11 cycle; non-overlapping.
 {4, 3, 10, 0, 8, 13, 5, 11, 1, 6, 14, 15, 2, 7, 9, 12,  },
 {1, 15, 4, 10, 6, 13, 5, 14, 11, 3, 7, 0, 9, 12, 2, 8,  },
 {3, 15, 7, 2, 8, 12, 0, 6, 1, 14, 5, 9, 13, 11, 4, 10,  },
 {15, 5, 13, 1, 11, 12, 14, 8, 10, 0, 6, 7, 4, 9, 3, 2,  },
 {7, 9, 13, 2, 6, 3, 12, 4, 14, 5, 1, 8, 0, 11, 15, 10,  },
 {14, 10, 13, 6, 3, 9, 12, 2, 4, 0, 7, 5, 1, 15, 11, 8,  },
 {15, 3, 6, 9, 11, 4, 12, 1, 7, 8, 5, 14, 0, 2, 13, 10,  },
 {11, 9, 14, 6, 3, 15, 8, 1, 5, 13, 7, 12, 4, 10, 0, 2,  },
 /*
 {11, 6, 8, 1, 3, 2, 10, 9, 7, 5, 14, 13, 0, 4, 15, 12,  },
 {8, 6, 10, 14, 5, 0, 7, 12, 1, 4, 13, 15, 3, 11, 9, 2,  },
 {9, 12, 5, 7, 6, 0, 13, 4, 10, 11, 1, 14, 2, 3, 15, 8,  },
 {2, 0, 6, 13, 11, 8, 5, 12, 10, 1, 14, 3, 9, 15, 7, 4,  },
 {1, 9, 11, 6, 14, 4, 12, 3, 2, 15, 5, 10, 13, 7, 0, 8,  },
 {12, 15, 1, 0, 5, 6, 14, 13, 4, 10, 7, 3, 2, 11, 8, 9,  },
 {10, 12, 1, 7, 5, 11, 0, 6, 14, 4, 15, 13, 3, 9, 2, 8,  },
 {1, 2, 15, 0, 11, 10, 13, 3, 9, 12, 14, 7, 4, 5, 6, 8,  },
 */
 // maximal cycles
  {11, 0, 4, 9, 14, 12, 2, 1, 7, 8, 5, 15, 13, 6, 3, 10},
  {14, 0, 6, 9, 12, 13, 5, 2, 10, 1, 4, 3, 15, 8, 7, 11},
  {9, 8, 7, 4, 5, 13, 3, 11, 10, 6, 2, 12, 14, 15, 0, 1},
 /*
  {1, 10, 8, 9, 5, 3, 13, 6, 4, 14, 7, 0, 2, 15, 11, 12},
  {11, 4, 15, 10, 8, 1, 0, 9, 12, 5, 14, 3, 6, 7, 2, 13},
  {11, 10, 15, 5, 0, 12, 4, 13, 14, 1, 3, 9, 2, 6, 7, 8},
  {6, 10, 3, 4, 15, 1, 8, 0, 9, 13, 2, 5, 14, 11, 7, 12},
  {3, 4, 15, 12, 9, 13, 5, 11, 2, 0, 6, 10, 8, 1, 7, 14},
  {5, 6, 3, 12, 11, 2, 9, 13, 7, 15, 0, 8, 14, 1, 4, 10},
  {3, 2, 9, 10, 14, 7, 4, 15, 12, 5, 13, 1, 6, 8, 11, 0},
  {3, 15, 12, 1, 11, 6, 9, 0, 14, 2, 5, 7, 13, 8, 4, 10},
  {3, 10, 12, 8, 13, 1, 9, 11, 6, 4, 2, 14, 7, 5, 15, 0},
  {2, 3, 10, 14, 8, 0, 5, 4, 1, 7, 13, 9, 11, 15, 6, 12},
  {5, 11, 1, 13, 3, 15, 0, 6, 9, 12, 4, 10, 14, 8, 7, 2},
  {15, 13, 10, 11, 12, 4, 0, 3, 1, 14, 8, 6, 2, 7, 5, 9},
  {13, 12, 7, 1, 2, 4, 3, 0, 5, 11, 6, 10, 15, 9, 8, 14},
 */

 // 13+3
 {4, 15, 6, 0, 3, 13, 12, 1, 7, 11, 14, 2, 5, 10, 8, 9,  },
 {3, 13, 10, 7, 11, 12, 8, 0, 2, 15, 9, 5, 6, 4, 1, 14,  },
 {12, 13, 9, 1, 10, 3, 2, 0, 5, 15, 8, 14, 7, 6, 4, 11,  },
 {13, 4, 11, 14, 2, 6, 7, 1, 3, 0, 5, 15, 8, 9, 10, 12,  },
 {5, 8, 15, 1, 12, 7, 4, 2, 3, 10, 6, 0, 11, 9, 13, 14,  },
 /*
 {4, 7, 15, 2, 6, 8, 11, 9, 10, 1, 14, 12, 13, 5, 3, 0,  },
 {14, 11, 3, 6, 7, 13, 9, 5, 0, 12, 4, 15, 8, 2, 10, 1,  },
 {3, 5, 15, 12, 0, 11, 4, 14, 6, 1, 2, 13, 9, 7, 8, 10,  },
 {1, 12, 9, 15, 3, 14, 7, 0, 13, 5, 8, 2, 10, 11, 6, 4,  },
 {14, 5, 8, 12, 1, 2, 11, 3, 6, 13, 9, 0, 7, 4, 15, 10,  },
 {12, 9, 0, 14, 13, 4, 10, 3, 2, 6, 15, 1, 7, 5, 11, 8,  },
 {10, 3, 12, 9, 14, 4, 15, 2, 7, 13, 1, 0, 11, 6, 5, 8,  },
 {12, 14, 11, 8, 13, 2, 4, 0, 7, 3, 15, 1, 10, 6, 9, 5,  },
 {4, 10, 6, 12, 14, 9, 8, 1, 7, 11, 0, 5, 13, 15, 3, 2,  },
 {2, 15, 11, 6, 8, 14, 12, 10, 0, 7, 4, 3, 1, 5, 13, 9,  },
 {10, 12, 6, 11, 5, 3, 13, 8, 9, 7, 15, 2, 14, 1, 0, 4,  },
 */
};

// Permutations.
static Table tables[16];

static inline half GetHalf(uint16 u) {
  return GradUtil::GetHalf(u);
}

static inline uint16 GetU16(half h) {
  return GradUtil::GetU16(h);
}

// Run one of the 16 substitution tables. A function from
// (-1, 1) to (-1, 1).
static half Subst(int n, half h) {
  const half HALF_GRID = (half)(0.5 / (Choppy::GRID * 2));
  return GetHalf(tables[n][GetU16(h)]) + HALF_GRID;
}

static Table mod_table;
static half ModularPlus(half x, half y) {
  const half HALF_GRID = (half)(0.5 / (Choppy::GRID * 2));
  // subtracting out one of the two HALF_GRIDs so that we don't
  // round up
  half sum = x + y - HALF_GRID;
  CHECK(sum >= (half)-2 && sum < (half)2) << sum;
  half msum = Exp::GetHalf(mod_table[Exp::GetU16(sum)]);
  /*
  printf("%.11g + %.11g = %.11g mod to %.11g\n",
         (float)x, (float)y, (float)sum, (float)msum);
  */
  return msum;
}

struct HashState {
  // Four halves in (-1,1].
  half a = GetHalf(0x38b9);
  half b = GetHalf(0x2e36);
  half c = GetHalf(0x159c);
  half d = GetHalf(0x00bc);

  half e = 3.0_h / ((half)Choppy::GRID / 2.0_h);
  half f = 1.0_h / ((half)Choppy::GRID / 2.0_h);
  half g = -4.0_h / ((half)Choppy::GRID / 2.0_h);
  half h = -5.0_h / ((half)Choppy::GRID / 2.0_h);
};

// h in [-1, 1).
static inline uint8_t HalfBits(half h) {
  // put in [0, 2)
  h += 1.0_h;
  // now in [0, 16)
  h *= (Choppy::GRID / 2.0_h);
  // and make integral
  return (uint8_t)trunc(h);
}

static uint32_t AllBits(HashState hs) {
  uint32_t ret = 0;

  ret <<= 4; ret |= HalfBits(hs.a);
  ret <<= 4; ret |= HalfBits(hs.b);
  ret <<= 4; ret |= HalfBits(hs.c);
  ret <<= 4; ret |= HalfBits(hs.d);
  ret <<= 4; ret |= HalfBits(hs.e);
  ret <<= 4; ret |= HalfBits(hs.f);
  ret <<= 4; ret |= HalfBits(hs.g);
  ret <<= 4; ret |= HalfBits(hs.h);

  return ret;
}

string StateString(HashState s) {
  return StringPrintf("[%.5g %.5g %.5g %.5g %.5g %.5g %.5g %.5g]",
                      (float)s.a,
                      (float)s.b,
                      (float)s.c,
                      (float)s.d,
                      (float)s.e,
                      (float)s.f,
                      (float)s.g,
                      (float)s.h);
}

HashState NextState(HashState s) {
  // Only linear functions!

  CHECK(s.a > -1.0_h && s.a < 1.0_h) << s.a;
  CHECK(s.b > -1.0_h && s.b < 1.0_h) << s.b;
  CHECK(s.c > -1.0_h && s.c < 1.0_h) << s.c;
  CHECK(s.d > -1.0_h && s.d < 1.0_h) << s.d;
  CHECK(s.e > -1.0_h && s.e < 1.0_h) << s.e;
  CHECK(s.f > -1.0_h && s.f < 1.0_h) << s.f;
  CHECK(s.g > -1.0_h && s.g < 1.0_h) << s.g;
  CHECK(s.h > -1.0_h && s.h < 1.0_h) << s.h;

  // red
  half aa = Subst(0, s.b);
  // green
  half bb = Subst(1, s.c);
  // blue
  half cc = Subst(2, s.d);
  // purple
  half dd = Subst(3, s.e);

  half ee = Subst(4, s.f);
  half ff = Subst(5, s.g);
  half gg = Subst(6, s.h);
  half hh = Subst(7, s.a);

  aa = Subst(8, aa);
  cc = Subst(9, cc);
  ee = Subst(10, ee);
  hh = Subst(11, hh);


  bb = ModularPlus(bb, gg);

  CHECK(aa > -1.0_h && aa < 1.0_h) << StateString(s) << " " << aa;
  CHECK(bb > -1.0_h && bb < 1.0_h) << StateString(s) << " " << bb;
  CHECK(cc > -1.0_h && cc < 1.0_h) << StateString(s) << " " << cc;
  CHECK(dd > -1.0_h && dd < 1.0_h) << StateString(s) << " " << dd;
  CHECK(ee > -1.0_h && ee < 1.0_h) << StateString(s) << " " << ee;
  CHECK(ff > -1.0_h && ff < 1.0_h) << StateString(s) << " " << ff;
  CHECK(gg > -1.0_h && gg < 1.0_h) << StateString(s) << " " << gg;
  CHECK(hh > -1.0_h && hh < 1.0_h) << StateString(s) << " " << hh;

  s.a = aa;
  s.b = bb;
  s.c = cc;
  s.d = dd;
  s.e = ee;
  s.f = ff;
  s.g = gg;
  s.h = hh;

  return s;
}

static std::vector<const Exp *> MakeExps(DB *basis) {
  std::vector<const Exp *> ret;
  for (int p = 0; p < 16; p++) {
    std::vector<const Exp *> parts;
    for (int col = 0; col < 16; col++) {
      // magnitude at this position (as number of GRID cells).
      int m = PERMS[p][col] - 8;
      if (m != 0) {
        DB::key_type key = DB::BasisKey(col);
        auto it = basis->fns.find(key);
        CHECK(it != basis->fns.end()) << "Incomplete basis: "
                                      << DB::KeyString(key);
        const Exp *e = it->second;
        if (m != 1) {
          e = basis->alloc.TimesC(e, Exp::GetU16((half)m), 1);
        }
        parts.push_back(e);
      }
    }
    CHECK(!parts.empty()) << "Function was zero everywhere?";
    const Exp *perm = parts[0];
    for (int i = 1; i < parts.size(); i++) {
      perm = basis->alloc.PlusE(perm, parts[i]);
    }
    ret.push_back(perm);
  }
  return ret;
}

static void InitTables(const std::vector<const Exp *> &exps) {
  CHECK(exps.size() == 16);
  //  for (int i = 0; i < exps.size(); i++) {
  ParallelComp(16, [&](int i) {
      CPrintf("Table " ACYAN("%d") ":\n", i);
      tables[i] = Exp::TabulateExpression(exps[i]);
    }, 4);
}

static void InitModTable() {
  Allocator alloc;

  static const char *ZERO_THRESHOLD = "V P02011 T3c019743 T07e01 P2fff1 T6c011 P5ff81 T44001 P3c001 T3c01559 T39031 T3c011160 T35421 T3c0123 T39dc1 T3c01137 T371e1 T3c01365 T39e61 T3c01346 T39a21 T3c01676 T38641 T3c01557 T39081 T3c01830 T329a1 T3c01336 T3a051 T3c01663 T1f111 Pe94f1 P694f1 T2b801 P64341 P68f31 Peb0d1 T34401 Pe8001 P68001 T30001";

  const Exp *Z = Exp::Deserialize(&alloc, ZERO_THRESHOLD);

  // Returns 0 for x < [-2, 1), and 1 for [1, 2).
  const Exp *H =
    alloc.TimesC(
        Exp::Subst(&alloc,
                   Z,
                   alloc.TimesC(
                       alloc.PlusC(alloc.Var(), Exp::GetU16((half)-1.0)),
                       Exp::GetU16((half)0.5))),
        Exp::GetU16((half)8.0));

  // Returns -1 for x < [-2, -1) and 0 for [-1, 2).
  const Exp *L =
      alloc.PlusC(
          alloc.TimesC(
              Exp::Subst(&alloc,
                         Z,
                         alloc.TimesC(
                             alloc.PlusC(alloc.Var(), Exp::GetU16((half)+1.0)),
                             // Scale down more
                             Exp::GetU16((half)0.25))),
              Exp::GetU16((half)8.0)),
          // and here we need to offset, giving -1 _____----- 0
          //                          instead of 0 -----~~~~~ 1
          Exp::GetU16((half)-1));

  mod_table = Exp::TabulateExpression(
      alloc.PlusE(
          // The original expression, e.g. (x + y)
          alloc.Var(),
          // ... but with corrections if we go too high or low
          alloc.TimesC(alloc.PlusE(H, L), Exp::GetU16((half)-2.0))));
}

int main(int argc, char **argv) {
  AnsiInit();
  DB db;
  db.LoadFile("basis.txt");

  std::vector<const Exp *> perm_exps = MakeExps(&db);
  InitTables(perm_exps);

  printf("Initialized 16 tables.\n");
  InitModTable();
  printf("Initialized mod table.\n");

  {
    static constexpr int IMG_WIDTH = 1920 / 2;
    static constexpr int PLOT_HEIGHT = 1080 / 2;
    static constexpr int BITS_HEIGHT = 32;
    static constexpr int IMG_HEIGHT = PLOT_HEIGHT + BITS_HEIGHT;
    ImageRGBA out(IMG_WIDTH, IMG_HEIGHT);
    out.Clear32(0x000000FF);

    HashState hs;
    for (int x = 0; x < IMG_WIDTH; x++) {
      auto Plot = [&out, x](half h, uint32_t color) {
          double y = (PLOT_HEIGHT / 2.0) - h * ((PLOT_HEIGHT * 0.9) / 2.0);
          // printf("h: %.11g   plot: %.11g\n", (double)h, (double)y);
          out.BlendPixel32(x, std::clamp((int)std::round(y), 0, PLOT_HEIGHT - 1),
                           color);
        };

      Plot(0.0_h, 0xFFFFFF22);
      Plot(1.0_h, 0xFFFFFF22);

      // printf("A: %.11g\n", (float)hs.a);
      Plot(hs.a, 0xFF0000AA);
      Plot(hs.b, 0x00FF00AA);
      Plot(hs.c, 0x0000FFAA);
      Plot(hs.d, 0xAA33AAAA);
      hs = NextState(hs);

      uint32_t u32 = AllBits(hs);
      for (int y = 0; y < 32; y++) {
        bool b = !!(u32 & (1 << (31 - y)));
        out.SetPixel32(x, PLOT_HEIGHT + y, b ? 0xFFFFFFFF : 0x333333FF);
      }
    }
    out.ScaleBy(2).Save("hash.png");
    printf("Wrote " ACYAN("hash.png") "\n");
  }


  // XXX generate byte stats as we go; abort early if it obviously sux
  if (true)  {

    int64 byte_count[256] = {};
    int64 bytes_counted = 0;
    uint8_t current_byte = 0;
    int current_bits = 0;

    // Generate a bitstream for statistical testing.
    static constexpr int SIZE_BYTES = 205280000;
    HashState hs;
    BitBuffer bb;
    bb.Reserve(SIZE_BYTES * 8);
    Timer timer;
    // 16 bits at a time
    while (bb.NumBits() < SIZE_BYTES * 8) {
      int64 nb = bb.NumBits();
      if ((nb % (1 << 20)) == 0) {
        CPrintf("%d" AGREY("/") "%d (" AGREEN("%.2f%%") ")\n", nb,
               SIZE_BYTES * 8, (nb * 100.0) / (SIZE_BYTES * 8));
      }
      hs = NextState(hs);
      // bb.WriteBits(16, AllBits(hs));
      uint8 bit = HalfBits(hs.a) & 1;
      bb.WriteBit(bit);

      current_byte <<= 1;
      current_byte |= bit;
      current_bits++;
      if (current_bits == 8) {
        byte_count[current_byte]++;
        current_bits = 0;
        current_byte = 0;
        bytes_counted++;

        static constexpr int SANITY_BYTES = 1000000;
        if (bytes_counted == SANITY_BYTES) {
          int64 target = SANITY_BYTES / 256;
          for (int i = 0; i < 256; i++) {
            if (byte_count[i] < (0.9 * target) ||
                byte_count[i] > (1.1 * target)) {
              printf("Exiting because of terrible distribution.\n"
                     "Byte " ACYAN("0x%02x") " appears "
                     ARED("%lld") " times (should be like "
                     ABLUE("%lld") ").\n", i, byte_count[i],
                     target);
              return 1;
            }
          }
          CPrintf("Sanity check looks " AGREEN("OK") "!\n");
        }
      }
    }
    double bytes_per_sec = SIZE_BYTES / timer.Seconds();
    CPrintf("Throughput: " ABLUE("%.5f") " bytes/sec\n", bytes_per_sec);
    Util::WriteFileBytes("hash.bin", bb.GetBytes());
    printf("Wrote hash.bin\n");
  }

  return 0;
}
