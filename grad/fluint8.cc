#include "fluint8.h"

#include <cstdint>

#include "half.h"
#include "expression.h"
#include "choppy.h"

using namespace half_float;
using namespace half_float::literal;

using Allocator = Exp::Allocator;

static constexpr int CHOPPY_GRID = 256;
using Choppy = ChoppyGrid<CHOPPY_GRID>;
using DB = Choppy::DB;

int64_t Fluint8::num_cheats = 0;

#if !FLUINT8_WRAP

uint16_t Fluint8::Representation() const { return Exp::GetU16(h); }

static Choppy::DB *GetDB() {
  static Choppy::DB *db = []() {
      auto *db = new Choppy::DB;
      db->LoadFile("basis8.txt");
      return db;
    }();
  return db;
}

static Allocator *GetAlloc() {
  return &GetDB()->alloc;
}

static const Exp *CanonicalizeExp() {
  static const Exp *canon = []() {
      printf("Load canon.txt\n");
      const Exp *e =
        Exp::Deserialize(GetAlloc(), Util::ReadFile("canon.txt"));
      printf("GOT %p\n", e);
      return e;
    }();
  CHECK(canon != nullptr);
  return canon;
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

half Fluint8::Canonicalize(half z) {
  return Eval(CanonicalizeExp(), z);
}

// This only works when in canonical form, but it's much
// faster than the old approach of using Canonicalize!
Fluint8 Fluint8::RightShift1(Fluint8 x) {
  return Fluint8((x.h * 0.5_h - 0.25_h) + 1024.0_h - 1024.0_h);
}


const std::vector<const Exp *> &Fluint8::BitExps() {
  static std::vector<const Exp *> bitexps = []() {
      static Choppy::DB *db = GetDB();
      auto RequireKey = [](const DB::key_type &key) -> const Exp * {
          auto it = db->fns.find(key);
          CHECK(it != db->fns.end());
          return it->second;
        };

      // To do bitwise ops, we select each bit from each input
      // using the basis functions. Then for example
      std::vector<const Exp *> bitexps;
      for (int bit = 0; bit < 8; bit++) {
        // Get all keys (byte values) that have this bit set.
        std::vector<const Exp *> select;
        for (int byte = 0; byte < 256; byte++) {
          if (byte & (1 << bit)) {
            auto key = DB::BasisKey(byte);
            const Exp *e = RequireKey(key);
            select.push_back(e);
          }
        }
        // Half the bytes have this bit set, by definition.
        CHECK(select.size() == 128);
        // Add them all together, but scale so that the expression
        // evaluates to either 1.0 (bit is set) or 0.0.
        const Exp *e =
          db->alloc.TimesC(db->alloc.PlusV(select),
                           // 128.0_h
                           0x5800);
        bitexps.push_back(e);
      }

      return bitexps;
    }();
  return bitexps;
}

// Returns the bits (as an integral half in [0, 255]) that are in
// common between the args, i.e. a & b.
half Fluint8::GetCommonBits(Fluint8 a, Fluint8 b) {
  static const std::vector<const Exp *> &bitexps = BitExps();

  // Get to the chopa
  const half chopa = a.ToChoppy();
  const half chopb = b.ToChoppy();

  // printf("%.2f & %.2f\n", (float)a.h, (float)b.h);

  // PERF: We can do the same shift trick here
  std::array<half, 8> abit, bbit, obit;
  for (int bit = 0; bit < 8; bit++) {
    // PERF no need to save these intermediates
    abit[bit] = GetHalf(Exp::EvaluateOn(bitexps[bit], GetU16(chopa)));
    bbit[bit] = GetHalf(Exp::EvaluateOn(bitexps[bit], GetU16(chopb)));
    // Multiplication is like AND.
    obit[bit] = abit[bit] * bbit[bit];

    /*
    printf("bit %d = %d: a=%.3f b=%.3f o=%.3f\n",
           bit, (1 << bit),
           (float)abit[bit], (float)bbit[bit], (float)obit[bit]);
    */
  }

  // Now obit[i] is 1.0 if both a and b had that bit set. So just
  // compute the resulting fluint. We compute the native (0-255)
  // representation directly.
  half common_bits = 0.0_h;
  for (int bit = 0; bit < 8; bit++) {
    half scale = (half)(1 << bit);
    common_bits += obit[bit] * scale;
  }

  return common_bits;
}

Fluint8 Fluint8::BitwiseAnd(Fluint8 a, Fluint8 b) {
  return Fluint8(GetCommonBits(a, b));
}

Fluint8 Fluint8::BitwiseOr(Fluint8 a, Fluint8 b) {
  half common_bits = GetCommonBits(a, b);
  // Neither the subtraction nor addition can overflow here, so
  // we can just do this directly without corrections.
  half result = (a.h - common_bits) + b.h;

  return Fluint8(result);
}

Fluint8 Fluint8::BitwiseXor(Fluint8 a, Fluint8 b) {
  half common_bits = GetCommonBits(a, b);
  // XOR is just the bits that the two do NOT have in common.
  half result = (a.h - common_bits) + (b.h - common_bits);
  return Fluint8(result);
}

half_float::half Fluint8::ToChoppy() const {
  return h * (1.0_h / 128.0_h) - 1.0_h;
}

Fluint8 Fluint8::FromChoppy(half_float::half h) {
  return Fluint8((h + 1.0_h) * 128.0_h);
}

void Fluint8::Warm() {
  (void)CanonicalizeExp();
  (void)BitExps();
  (void)Fluint8::Plus(Fluint8(1), Fluint8(2));
  (void)Fluint8::Minus(Fluint8(3), Fluint8(4));
}

#endif
