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

// Allocates each time.
static const Exp *MakeMod() {
  Allocator *alloc = GetAlloc();
  static const char *ZERO_THRESHOLD = "V P02011 T3c019743 T07e01 P2fff1 T6c011 P5ff81 T44001 P3c001 T3c01559 T39031 T3c011160 T35421 T3c0123 T39dc1 T3c01137 T371e1 T3c01365 T39e61 T3c01346 T39a21 T3c01676 T38641 T3c01557 T39081 T3c01830 T329a1 T3c01336 T3a051 T3c01663 T1f111 Pe94f1 P694f1 T2b801 P64341 P68f31 Peb0d1 T34401 Pe8001 P68001 T30001";

  const Exp *Z = Exp::Deserialize(alloc, ZERO_THRESHOLD);

  // Returns 0 for x < [-2, 1), and 1 for [1, 2).
  const Exp *H =
    alloc->TimesC(
        Exp::Subst(alloc,
                   Z,
                   alloc->TimesC(
                       alloc->PlusC(alloc->Var(), Exp::GetU16((half)-1.0)),
                       Exp::GetU16((half)0.5))),
        Exp::GetU16((half)8.0));

  // Returns -1 for x < [-2, -1) and 0 for [-1, 2).
  const Exp *L =
      alloc->PlusC(
          alloc->TimesC(
              Exp::Subst(alloc,
                         Z,
                         alloc->TimesC(
                             alloc->PlusC(alloc->Var(),
                                          Exp::GetU16((half)+1.0)),
                             // Scale down more
                             Exp::GetU16((half)0.25))),
              Exp::GetU16((half)8.0)),
          // and here we need to offset, giving -1 _____----- 0
          //                          instead of 0 -----~~~~~ 1
          Exp::GetU16((half)-1));

  return alloc->PlusE(
      // The original expression, e.g. (x + y)
      alloc->Var(),
      // ... but with corrections if we go too high or low
      alloc->TimesC(alloc->PlusE(H, L), Exp::GetU16((half)-2.0)));
}

static const Exp *Mod() {
  static const Exp *mod = MakeMod();
  return mod;
}

// h in [-1, 1). Returns the corresponding byte.
static inline uint8_t HalfToBits(half h) {
  // put in [0, 2)
  h += 1.0_h;
  // now in [0, 16)
  h *= (CHOPPY_GRID / 2.0_h);
  // and make integral
  return (uint8_t)trunc(h);
}

// The centered value for a given byte.
static inline half BitsToHalf(uint8_t b) {
  half h = (half)(int)b;
  h *= 2.0_h / CHOPPY_GRID;
  h -= 1.0_h;
  const half HALF_GRID = (half)(0.5 / (CHOPPY_GRID * 2));
  h += HALF_GRID;
  return h;
}

Fluint8::Fluint8(uint8_t b) {
  h = BitsToHalf(b);
}

uint8_t Fluint8::ToInt() const {
  return HalfToBits(h);
}

Fluint8 Fluint8::Eval(const Exp *exp, Fluint8 x) {
  uint16_t u = Exp::EvaluateOn(exp, Exp::GetU16(x.h));
  return Fluint8(Exp::GetHalf(u));
}

// For functions of multiple variables, we allow some linear
// functions of these. Note we should be careful when multiplying,
// since e.g. x * x is not linear!
Fluint8 Fluint8::Minus(Fluint8 x, Fluint8 y) {
  const half HALF_GRID = (half)(0.5 / (CHOPPY_GRID * 2));
  const half xx = ((x.h - HALF_GRID) + 1.0_h) * 0.5_h;
  const half yy = ((y.h - HALF_GRID) + 1.0_h) * 0.5_h;

  // Difference is in [-1, 1].
  const half diff = xx - yy;
  // And then in [-3, 1].
  const half adiff = (diff * 2.0_h) - 1.0_h;
  const half mdiff = Eval(Mod(), Fluint8(adiff)).h;
  return Fluint8(mdiff + HALF_GRID);
}

Fluint8 Fluint8::Plus(Fluint8 x, Fluint8 y) {
  const half HALF_GRID = (half)(0.5 / (CHOPPY_GRID * 2));
  // x and y are in [-1,1) but we want to treat them as numbers
  // in [0,1).
  const half xx = ((x.h - HALF_GRID) + 1.0_h) * 0.5_h;
  const half yy = ((y.h - HALF_GRID) + 1.0_h) * 0.5_h;

  // now the sum is in [0, 2).
  const half sum = xx + yy;
  // Now in [-1, 3).
  const half asum = (sum * 2.0_h) - 1.0_h;
  const half msum = Eval(Mod(), Fluint8(asum)).h;
  return Fluint8(msum + HALF_GRID);
}
