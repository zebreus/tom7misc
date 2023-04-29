
#include <cstdint>

#include "half.h"
#include "grad-util.h"

#include "expression.h"

using Allocator = Exp::Allocator;

static constexpr bool VERBOSE = false;

// lo must be negative, hi positive.
template<class F>
static inline void ForNegToPosAscending(uint16_t lo, uint16_t hi,
                                        int stride,
                                        F f) {
  // Negative
  for (int u = lo; u >= GradUtil::NEG_LOW; u -= stride)
    f((uint16)u);
  // Positive
  for (int u = GradUtil::POS_LOW; u <= hi; u += stride)
    f((uint16)u);
}


static void AllZT88() {
  Allocator *alloc = new Allocator;

  // Note I removed the T30001 at the end, so this outputs 1, not 1/8.
  // static const char *ZERO_THRESHOLD = "V P02011 T3c019743 T07e01 P2fff1 T6c011 P5ff81 T44001 P3c001 T3c01559 T39031 T3c011160 T35421 T3c0123 T39dc1 T3c01137 T371e1 T3c01365 T39e61 T3c01346 T39a21 T3c01676 T38641 T3c01557 T39081 T3c01830 T329a1 T3c01336 T3a051 T3c01663 T1f111 Pe94f1 P694f1 T2b801 P64341 P68f31 Peb0d1 T34401 Pe8001 P68001";

  // static const char *ZERO_THRESHOLD = "V P02011 T3c019743 T07e01 P2fff1 T6c011 P5ff81 T44001 P3c051 T3c01539 T39031 T3c011160 T35421 T3c0423 T39df1 T3c01137 T371e1 T3c01365 T39e61 T3c03346 T39a21 T3c01676 T38631 T3c01550 T39081 T3c01830 T32991 T3c01336 T3a053 T3c01666 T1f111 Pe94b1 P694a1 T2b801 P64341 P68f31 Peb0d1 T34401 Pe8001 P68001";

  //  static const char *ZERO_THRESHOLD = "V P02011 T3c009743 T07e01 P2fff1 T6c011 P5ff81 T44001 P3c001 T3bfe539 T38fb1 T3c011160 T35421 T3c0423 T39df1 T3c01137 T371e1 T3c03365 T39e61 T3c03346 T39a21 T3c01676 T38631 T3c01550 T39041 T3c01816 T32991 T3c02336 T39fd3 T3c01666 T1f111 Pe94b1 P694a1 T2b801 P64341 P68f31 Peb0d1 T34401 Pe8001 P68001";

  static const char *ZERO_THRESHOLD = "V P02011 T3c019743 T07e01 P2fff1 T6c010 P5fff1 T44001 P3c051 T3c01539 T39071 T3c011160 T35421 T3c0423 T39df1 T3c01137 T371e1 T3c03365 T39e61 T3c03346 T39a21 T3c01676 T385c1 T3c01550 T39081 T3c01830 T32991 T3c01336 T3a053 T3c01671 T1f111 Pe94b1 P694a1 T2b801 P64341 P68f31 Peb0d1 T34401 Pe8001 P68001";

  // static constexpr uint16_t LOW = 0xc800; // GradUtil::GetU16((half)-8.0);
  // static constexpr uint16_t HIGH = 0x4800; // GradUtil::GetU16((half)8.0);
  // static constexpr uint16_t LOW = 0xcc00; // -16
  // static constexpr uint16_t HIGH = 0x4c00; // +16
  static constexpr int STRIDE = 2;
  // static constexpr uint16_t LOW = 0xec00; // -4096
  // static constexpr uint16_t HIGH = 0x6c00; // +4096

  static constexpr uint16_t LOW = 0xfbff; // min finite
  static constexpr uint16_t HIGH = 0x7bff; // max finite

  // How many values?
  int num_values = 0;
  ForNegToPosAscending(LOW, HIGH, STRIDE, [&num_values](uint16_t u) {
      num_values++;
    });
  printf("There are %d values.\n", num_values);

  string err;
  const Exp *zt_exp = Exp::Deserialize(alloc, ZERO_THRESHOLD, &err);
  CHECK(zt_exp != nullptr) << err;

  Exp::Table zt = Exp::TabulateExpression(zt_exp);

  ImageRGBA img(num_values, num_values);

  int num = 0, num_wrong = 0;
  int max_wrong = 0;
  int y = 0;
  ForNegToPosAscending(LOW, HIGH, STRIDE,
      [&zt, &img, &num, &num_wrong, &max_wrong, &y](uint16_t uthresh) {
      half thresh = GradUtil::GetHalf(uthresh);

      const bool t11 = thresh >= GradUtil::GetHalf(0xbc00) &&
        thresh <= GradUtil::GetHalf(0x3c00);

      int wrong_count = 0;
      int x = 0;
      ForNegToPosAscending(LOW, HIGH, STRIDE,
        [&zt, &img, uthresh, thresh, &wrong_count, t11, y, &x](uint16_t uv) {
          half v = GradUtil::GetHalf(uv);

          const bool in11 = t11 &&
            v >= GradUtil::GetHalf(0xbc00) &&
            v <= GradUtil::GetHalf(0x3c00);

          // f(v) = zt(v - thresh)

          uint16_t uin = GradUtil::GetU16(v - thresh);
          uint16_t uout = zt[uin];

          bool wrong = false;
          if (v >= thresh) {
            // Expect exactly 1.0
            if (uout != 0x3c00) {
              if (VERBOSE && !wrong_count) {
                printf("[1] For thresh %04x = %.6f on %04x = %.6f, got "
                       "%04x = %.6f\n", uthresh, (float)thresh,
                       uv, (float)v, uout, (float)Exp::GetHalf(uout));
              }
              wrong = true;
            }
          } else {
            if (uout != 0x0000 && uout != 0x8000) {
              if (VERBOSE && !wrong_count) {
                printf("[Z] For thresh %04x = %.6f on %04x = %.6f, got "
                       "%04x = %.6f\n", uthresh, (float)thresh,
                       uv, (float)v, uout, (float)Exp::GetHalf(uout));
              }
              wrong = true;
            }
          }

          static constexpr bool highlight = true;
          uint32_t okcolor = (highlight && in11) ? 0xFF5555FF : 0xAA0000FF;
          uint32_t nocolor = (highlight && in11) ? 0x55FF55FF : 0x00AA00FF;

          img.SetPixel32(x, y, wrong ? okcolor : nocolor);
          if (wrong) {
            wrong_count++;
          }

          x++;
        });

      num++;
      if (wrong_count > 0) {
        num_wrong++;
      }

      max_wrong = std::max(max_wrong, wrong_count);

      if (num % 1000 == 0) printf("%d (%d wrong)\n", num, num_wrong);
      y++;
    });

  printf("Total %d, wrong: %d\n", num, num_wrong);
  printf("Max wrong: %d\n", max_wrong);

  img.ScaleDownBy(4).Save("zt88.png");
}

static void AllZT11() {
  Allocator *alloc = new Allocator;

  // XXX original ZT
  // Note I removed the T30001 at the end, so this outputs 1, not 1/8.
  static const char *ZERO_THRESHOLD = "V P02011 T3c019743 T07e01 P2fff1 T6c011 P5ff81 T44001 P3c001 T3c01559 T39031 T3c011160 T35421 T3c0123 T39dc1 T3c01137 T371e1 T3c01365 T39e61 T3c01346 T39a21 T3c01676 T38641 T3c01557 T39081 T3c01830 T329a1 T3c01336 T3a051 T3c01663 T1f111 Pe94f1 P694f1 T2b801 P64341 P68f31 Peb0d1 T34401 Pe8001 P68001";

  const Exp *zt_exp = Exp::Deserialize(alloc, ZERO_THRESHOLD);

  Exp::Table zt = Exp::TabulateExpression(zt_exp);

  ImageRGBA img(30722, 30722);

  int num = 0, num_wrong = 0;
  int max_wrong = 0;
  int y = 0;
  GradUtil::ForNeg1To1Ascending(
      [&zt, &img, &num, &num_wrong, &max_wrong, &y](uint16_t uthresh) {
      half thresh = GradUtil::GetHalf(uthresh);

      int wrong_count = 0;
      int x = 0;
      GradUtil::ForNeg1To1Ascending(
          [&zt, &img, uthresh, thresh, &wrong_count, y, &x](uint16_t uv) {
          half v = GradUtil::GetHalf(uv);

          // f(v) = zt(v - thresh)

          uint16_t uin = GradUtil::GetU16(v - thresh);
          uint16_t uout = zt[uin];

          bool wrong = false;
          if (v >= thresh) {
            // Expect exactly 1.0
            if (uout != 0x3c00) {
              if (VERBOSE && !wrong_count) {
                printf("[1] For thresh %04x = %.6f on %04x = %.6f, got "
                       "%04x = %.6f\n", uthresh, (float)thresh,
                       uv, (float)v, uout, (float)Exp::GetHalf(uout));
              }
              wrong = true;
            }
          } else {
            if (uout != 0x0000 && uout != 0x8000) {
              if (VERBOSE && !wrong_count) {
                printf("[Z] For thresh %04x = %.6f on %04x = %.6f, got "
                       "%04x = %.6f\n", uthresh, (float)thresh,
                       uv, (float)v, uout, (float)Exp::GetHalf(uout));
              }
              wrong = true;
            }
          }

          img.SetPixel32(x, y, wrong ? 0xAA0000FF : 0x00AA00FF);
          if (wrong) {
            wrong_count++;
          }

          x++;
        });

      num++;
      if (wrong_count > 0) {
        num_wrong++;
      }

      max_wrong = std::max(max_wrong, wrong_count);

      if (num % 100 == 0) printf("%d (%d wrong)\n", num, num_wrong);
      y++;
    });

  printf("Total %d, wrong: %d\n", num, num_wrong);
  printf("Max wrong: %d\n", max_wrong);

  img.ScaleDownBy(4).Save("zt.png");
}


int main(int argc, char **argv) {
  // AllZT11();

  AllZT88();

  return 0;
}
