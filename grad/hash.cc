
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

// XXX continue porting this to 8-bit
using Choppy = ChoppyGrid<256>;
using Allocator = Exp::Allocator;
using DB = Choppy::DB;
using Table = GradUtil::Table;
using Step = GradUtil::Step;
using State = GradUtil::State;

static constexpr uint8_t SUBST[256] = {
// Best error was 1952 after 150000 attempts [438.966/sec]
  178, 122, 20, 19, 31, 64, 70, 35, 75, 80, 154, 5, 47, 105, 93, 250, 217, 140, 191, 233, 58, 79, 212, 216, 237, 4, 141, 101, 160, 181, 107, 206, 180, 220, 248, 28, 133, 142, 143, 204, 117, 1, 175, 86, 243, 232, 73, 90, 150, 203, 218, 8, 219, 2, 111, 57, 48, 46, 130, 33, 174, 171, 189, 158, 6, 11, 254, 193, 18, 76, 227, 137, 149, 106, 115, 207, 224, 170, 38, 30, 82, 108, 74, 9, 235, 112, 16, 129, 100, 15, 109, 183, 59, 165, 62, 77, 194, 14, 169, 116, 161, 155, 246, 213, 238, 94, 134, 92, 146, 255, 173, 69, 56, 24, 12, 249, 54, 145, 166, 121, 99, 53, 123, 131, 81, 251, 103, 202, 135, 211, 242, 244, 26, 148, 228, 209, 167, 182, 97, 185, 197, 223, 44, 60, 231, 51, 110, 230, 177, 236, 132, 126, 66, 138, 114, 40, 45, 214, 186, 156, 10, 200, 221, 7, 32, 23, 198, 29, 196, 91, 13, 225, 68, 61, 229, 21, 159, 78, 22, 187, 226, 43, 252, 27, 241, 25, 188, 136, 127, 184, 201, 102, 0, 253, 151, 34, 247, 222, 49, 71, 50, 36, 118, 195, 128, 72, 120, 190, 240, 52, 84, 208, 157, 104, 88, 205, 42, 124, 89, 95, 163, 144, 147, 113, 41, 83, 39, 199, 172, 96, 152, 17, 153, 125, 55, 192, 67, 3, 119, 63, 234, 87, 176, 245, 164, 85, 65, 215, 179, 168, 98, 139, 162, 210, 239, 37,
};

static inline half GetHalf(uint16 u) {
  return GradUtil::GetHalf(u);
}

static inline uint16 GetU16(half h) {
  return GradUtil::GetU16(h);
}

// Substitution table.
static Table subst_table;
// Run the substitution table. A function from
// [-1, 1) to [-1, 1).
static half Subst(half h) {
  const half HALF_GRID = (half)(0.5 / (Choppy::GRID * 2));
  return GetHalf(subst_table[GetU16(h)]) + HALF_GRID;
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

static half ModularMinus(half x, half y) {
  const half HALF_GRID = (half)(0.5 / (Choppy::GRID * 2));
  half sum = x - y + HALF_GRID;
  CHECK(sum >= (half)-2 && sum < (half)2) << sum;
  half msum = Exp::GetHalf(mod_table[Exp::GetU16(sum)]);
  return msum;
}


struct HashState {

  HashState() {
    // This is the constant for zero.
    const half HALF_GRID = (half)(0.5 / (Choppy::GRID * 2));
    a = HALF_GRID;
    b = HALF_GRID;
    c = HALF_GRID;
    d = HALF_GRID;
    e = HALF_GRID;
    f = HALF_GRID;
    g = HALF_GRID;
    h = HALF_GRID;
  }

  // Eight halves in (-1,1]. Each represents 8 bits of state.

  half a, b, c, d, e, f, g, h;
};

// TODO: Test that this works for every byte in
// the 8-bit version!
// h in [-1, 1). Returns the corresponding byte.
static inline uint8_t HalfBits(half h) {
  // put in [0, 2)
  h += 1.0_h;
  // now in [0, 16)
  h *= (Choppy::GRID / 2.0_h);
  // and make integral
  return (uint8_t)trunc(h);
}

static uint64_t AllBits(HashState hs) {
  uint64_t ret = 0;

  ret <<= 8; ret |= HalfBits(hs.a);
  ret <<= 8; ret |= HalfBits(hs.b);
  ret <<= 8; ret |= HalfBits(hs.c);
  ret <<= 8; ret |= HalfBits(hs.d);
  ret <<= 8; ret |= HalfBits(hs.e);
  ret <<= 8; ret |= HalfBits(hs.f);
  ret <<= 8; ret |= HalfBits(hs.g);
  ret <<= 8; ret |= HalfBits(hs.h);

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

  half aa = Subst(s.a);
  half bb = Subst(s.b);
  half cc = Subst(s.c);
  half dd = Subst(s.d);
  half ee = Subst(s.e);
  half ff = Subst(s.f);
  half gg = Subst(s.g);
  half hh = Subst(s.h);

  // TODO: Apply bit permutation.

  aa = ModularPlus(aa, bb);
  cc = ModularMinus(cc, bb);

  dd = ModularPlus(dd, ee);
  gg = ModularMinus(gg, ee);

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

static const Exp * MakeSubstExp(DB *basis) {

  std::vector<const Exp *> parts;
  for (int col = 0; col < Choppy::GRID; col++) {
    // magnitude at this position (as number of GRID cells).
    int m = SUBST[col] - Choppy::GRID/2;
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

  return perm;
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


static void InitTables(DB *basis) {
  InParallel(
      [&]() {
        subst_table = Exp::TabulateExpression(MakeSubstExp(basis));
        CPrintf("Make " ACYAN("subst") ".\n");
      },
      []() {
        InitModTable();
        CPrintf("Make " ACYAN("mod") ".\n");
      });
}

int main(int argc, char **argv) {
  AnsiInit();
  DB db;
  db.LoadFile("basis.txt");

  InitTables(&db);

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
