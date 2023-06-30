
#include <cstdio>

#include "expression.h"


using Table = Exp::Table;
using Allocator = Exp::Allocator;

static char HexChar(int value) {
  if (value < -8) return '_';
  if (value > 7) return '^';
  return "0123456789abcdef"[value + 8];
}

static int Abs(int c) {
  if (c < 0) return -c;
  return c;
}

static void PrintTables() {
  printf("\n");
  for (int y = -8; y < 7; y++) {
    for (int x = -8; x < 7; x++) {
      int c = x + y;
      if (c < -8) c += 16;
      if (c > 7) c -= 16;
      printf("%c ", HexChar(c));
    }
    printf("\n");
  }


  printf("-----\n");

  for (int y = -8; y < 7; y++) {
    for (int x = -8; x < 7; x++) {
      int a = Abs(x) - Abs(y);
      int b = Abs(y) - Abs(x);
      int c = a + b;
      printf("%c%c%c ", HexChar(a), HexChar(b), HexChar(c));
    }
    printf("\n");
  }
}

// Returns 0 for x < [-2, 1), and 1 for [1, 2).
static void ThresholdHigh() {
  Allocator alloc;

  // zero when <0, 1/8 when >= 0 (or -0).
  static const char *ZERO_THRESHOLD = "V P02011 T3c019743 T07e01 P2fff1 T6c011 P5ff81 T44001 P3c001 T3c01559 T39031 T3c011160 T35421 T3c0123 T39dc1 T3c01137 T371e1 T3c01365 T39e61 T3c01346 T39a21 T3c01676 T38641 T3c01557 T39081 T3c01830 T329a1 T3c01336 T3a051 T3c01663 T1f111 Pe94f1 P694f1 T2b801 P64341 P68f31 Peb0d1 T34401 Pe8001 P68001 T30001";

  const Exp *Z = Exp::Deserialize(&alloc, ZERO_THRESHOLD);

  // So if this were a real math function Z(x), it should work
  // to have H(x) = 8 * Z((x / 2) - 1)

  const Exp *H =
    alloc.TimesC(
        Exp::Subst(&alloc,
                   Z,
                   alloc.TimesC(
                       alloc.PlusC(alloc.Var(), Exp::GetU16((half)-1.0)),
                       Exp::GetU16((half)0.5))),
        Exp::GetU16((half)8.0));

  // Does it have the desired properties?

  const uint16_t end = Exp::GetU16((half)2.0);
  for (uint16_t xu = Exp::GetU16((half)-2.0); xu != end; xu = Exp::NextAfter16(xu)) {
    half x = Exp::GetHalf(xu);
    half y = Exp::GetHalf(Exp::EvaluateOn(H, xu));
    if (x < (half)1.0) {
      CHECK(y == (half)0.0) << x << " " << y;
    } else {
      CHECK(y == (half)1.0) << x << " " << y;
    }
  }
  printf("ThresholdHigh OK!\n");
}

// Like above, but -1 when < 1.
static void ThresholdLow() {
  Allocator alloc;

  static const char *ZERO_THRESHOLD = "V P02011 T3c019743 T07e01 P2fff1 T6c011 P5ff81 T44001 P3c001 T3c01559 T39031 T3c011160 T35421 T3c0123 T39dc1 T3c01137 T371e1 T3c01365 T39e61 T3c01346 T39a21 T3c01676 T38641 T3c01557 T39081 T3c01830 T329a1 T3c01336 T3a051 T3c01663 T1f111 Pe94f1 P694f1 T2b801 P64341 P68f31 Peb0d1 T34401 Pe8001 P68001 T30001";

  const Exp *Z = Exp::Deserialize(&alloc, ZERO_THRESHOLD);

  // So if this were a real math function Z(x), it should work
  // to have H(x) = 8 * Z((x / 2) - 1)

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

  // Does it have the desired properties?

  const uint16_t end = Exp::GetU16((half)2.0);
  for (uint16_t xu = Exp::GetU16((half)-2.0); xu != end; xu = Exp::NextAfter16(xu)) {
    half x = Exp::GetHalf(xu);
    half y = Exp::GetHalf(Exp::EvaluateOn(L, xu));
    if (x < (half)-1.0) {
      CHECK(y == (half)-1.0) << x << " " << y;
    } else {
      CHECK(y == (half)0.0) << x << " " << y;
    }
  }
  printf("ThresholdLow OK!\n");
}

Table mod_table;
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

  // Returns 1 for x < [-2, -1) and 0 for [-1, 2).
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


static void TestModPlus() {
  for (half x = (half)-2; x < (half)2.1; x += (half)0.125) {
    half y = Exp::GetHalf(mod_table[Exp::GetU16(x)]);
    printf("%.11g -> %.11g\n", (float)x, (float)y);
  }
}


int main(int argc, char **argv) {
  // PrintTables();
  ThresholdHigh();
  ThresholdLow();

  InitModTable();
  TestModPlus();

  return 0;
}
