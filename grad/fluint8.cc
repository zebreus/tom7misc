#include "fluint8.h"

#include <cstdint>

#include "half.h"
#include "expression.h"

using namespace half_float;
using namespace half_float::literal;

using Allocator = Exp::Allocator;

static constexpr int CHOPPY_GRID = 256;

uint16_t Fluint8::Representation() const { return Exp::GetU16(h); }

static Allocator *GetAlloc() {
  static Allocator *alloc = new Exp::Allocator;
  return alloc;
}

// Indicates (value is 1) if the variable is greater than or equal to
// the argument.
static const Exp *IndicateGreater(half h) {
  static Allocator *alloc = GetAlloc();
  // zero when <0, 1/8 when >= 0 (or -0).
  // (XXX I think this might be strictly greater than 0?)
  static const char *ZERO_THRESHOLD = "V P02011 T3c019743 T07e01 P2fff1 T6c011 P5ff81 T44001 P3c001 T3c01559 T39031 T3c011160 T35421 T3c0123 T39dc1 T3c01137 T371e1 T3c01365 T39e61 T3c01346 T39a21 T3c01676 T38641 T3c01557 T39081 T3c01830 T329a1 T3c01336 T3a051 T3c01663 T1f111 Pe94f1 P694f1 T2b801 P64341 P68f31 Peb0d1 T34401 Pe8001 P68001 T30001";
  static const Exp *Z = Exp::Deserialize(alloc, ZERO_THRESHOLD);

  return
    alloc->TimesC(
        Exp::Subst(alloc, Z, alloc->PlusC(alloc->Var(),
                                          Exp::GetU16((half)-h))),
        // TODO: Bake this into ZERO_THRESHOLD, yeah?
        // Scale to return 1, not 1/8.
        Exp::GetU16(8.0_h));
}

Fluint8::Fluint8(uint8_t b) {
  h = (int)b;
}

uint8_t Fluint8::ToInt() const {
  return (int)h;
}

half Fluint8::Eval(const Exp *exp, half h) {
  uint16_t u = Exp::EvaluateOn(exp, Exp::GetU16(h));
  return Exp::GetHalf(u);
}

// For functions of multiple variables, we allow some linear
// functions of these. Note we should be careful when multiplying,
// since e.g. x * x is not linear!
Fluint8 Fluint8::Plus(Fluint8 x, Fluint8 y) {
  Allocator *alloc = GetAlloc();
  // Correct value, except that it could be in [256,512).
  // We add 0.5 here since the indicator is "strictly greater",
  // not greater-eq.
  const half z = x.h + y.h + 0.5_h;

  // We have to be in a reasonable range for the indicator
  // function to work.
  const half zz = z * (1.0_h / 256.0_h);

  static const Exp *correct =
    alloc->PlusE(
        // The original expression, x + y, scaled into [0, 2).
        alloc->Var(),
        // but subtracting 1 if over 1.
        alloc->TimesC(IndicateGreater(1.0_h),
                      Exp::GetU16(-1.0_h)));
  const half s = Eval(correct, zz);

  /*
  printf("%.5f + %.5f + 0.5 = %.5f zz %.5f, s %.5f\n",
         (float)x.h, (float)y.h, (float)z,
         (float)zz,
         (float)s);
  */

  // Scale back to canonical range.
  return Fluint8(s * 256.0_h - 0.5_h);
}

Fluint8 Fluint8::Minus(Fluint8 x, Fluint8 y) {
  Allocator *alloc = GetAlloc();
  // This can work just like plus.
  const half z = x.h - y.h + 256.5_h;

  const half zz = z * (1.0_h / 256.0_h);

  static const Exp *correct =
    alloc->PlusE(
        // The original expression, x + y, scaled into [0, 2).
        alloc->Var(),
        // but subtracting 1 if over 1.
        alloc->TimesC(IndicateGreater(1.0_h),
                      Exp::GetU16(-1.0_h)));
  const half s = Eval(correct, zz);

  return Fluint8(s * 256.0_h - 0.5_h);
}
