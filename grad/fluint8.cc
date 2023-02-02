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
[[maybe_unused]]
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
  auto [carry, sum] = AddWithCarry(x, y);
  return sum;
}

std::pair<Fluint8, Fluint8> Fluint8::AddWithCarry(Fluint8 x, Fluint8 y) {
  // Correct value, but maybe 256 too high because of overflow.
  const half z = x.h + y.h;

  // Shift down 8 times to get the overflow bit.
  half o = RightShiftHalf8(z);

  CHECK(o == 1.0_h || o == 0.0_h) << o;
  return make_pair(Fluint8(o), Fluint8(z - o * 256.0_h));
}

std::pair<Fluint8, Fluint8> Fluint8::SubtractWithCarry(Fluint8 x, Fluint8 y) {
  const half z = x.h - y.h + 256.0_h;

  // Shift down 8 times to get the overflow bit.
  half o = RightShiftHalf8(z);

  CHECK(o == 1.0_h || o == 0.0_h) << o;
  return make_pair(Fluint8(1.0_h - o), Fluint8(z - o * 256.0_h));
}

Fluint8 Fluint8::Minus(Fluint8 x, Fluint8 y) {
  auto [carry, diff] = SubtractWithCarry(x, y);
  return diff;
}

half Fluint8::Canonicalize(half z) {
  return Eval(CanonicalizeExp(), z);
}

// This only works when in canonical form, but it's much
// faster than the old approach of using Canonicalize!
Fluint8 Fluint8::RightShift1(Fluint8 x) {
  return Fluint8(RightShiftHalf1(x.h));
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

/* This comment is a memorial to a great joke which is no
   longer necessary:

  // Get to the chopa
  const half chopa = a.ToChoppy();
*/

// Returns the bits (as an integral half in [0, 255]) that are in
// common between the args, i.e. a & b.
half Fluint8::GetCommonBits(Fluint8 a, Fluint8 b) {
  half common = (half)0.0f;
  Fluint8 aa = a, bb = b;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    // Low order bit as a - ((a >> 1) << 1)
    Fluint8 aashift = RightShift1(aa);
    Fluint8 bbshift = RightShift1(bb);
    half a_bit = aa.h - LeftShift1Under128(aashift).h;
    half b_bit = bb.h - LeftShift1Under128(bbshift).h;
    const half scale = (half)(1 << bit_idx);


    // Multiplication is like AND.
    // But note that if we tried to do GetCommonBits(z, z) here,
    // we would end up multiplying some function of z by some
    // function of z, which is not linear. So instead of
    // multiplying, we compute "a & b" as "(a + b) >> 1".
    // const half and_bits = (a_bit * b_bit);
    const half and_bits = RightShiftHalf1(a_bit + b_bit);
    // (Whereas scale is just a constant...)
    common += scale * and_bits;

    // and keep shifting down
    aa = aashift;
    bb = bbshift;
  }

  return common;
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

Fluint8 Fluint8::IsntZero(Fluint8 a) {
  // A simple way to do this is to extract all the (negated) bits
  // and multiply them together. But this would mean multiplying
  // a by itself, and so is not linear.
  //
  // Instead, do the same but ADD the bits. Now we have a
  // number in [0, 8]. So now we can do the whole thing again
  // and get a number in [0, 4], etc.

  half num_ones = (half)0.0f;
  Fluint8 aa = a;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    Fluint8 aashift = RightShift1(aa);
    half a_bit = aa.h - LeftShift1Under128(aashift).h;
    num_ones += a_bit;
    aa = aashift;
  }

  CHECK(num_ones >= (half)0.0f && num_ones <= (half)8.0f);

  // now count the ones in num_ones.
  aa = Fluint8(num_ones);
  num_ones = (half)0.0f;
  for (int bit_idx = 0; bit_idx < 4; bit_idx++) {
    Fluint8 aashift = RightShift1(aa);
    half a_bit = aa.h - LeftShift1Under128(aashift).h;
    num_ones += a_bit;
    aa = aashift;
  }

  CHECK(num_ones >= (half)0.0f && num_ones <= (half)4.0f);

  // and again ...
  aa = Fluint8(num_ones);
  num_ones = (half)0.0f;
  for (int bit_idx = 0; bit_idx < 3; bit_idx++) {
    Fluint8 aashift = RightShift1(aa);
    half a_bit = aa.h - LeftShift1Under128(aashift).h;
    num_ones += a_bit;
    aa = aashift;
  }

  CHECK(num_ones >= (half)0.0f && num_ones <= (half)3.0f);

  // and again ...
  aa = Fluint8(num_ones);
  num_ones = (half)0.0f;
  for (int bit_idx = 0; bit_idx < 2; bit_idx++) {
    Fluint8 aashift = RightShift1(aa);
    half a_bit = aa.h - LeftShift1Under128(aashift).h;
    num_ones += a_bit;
    aa = aashift;
  }

  CHECK(num_ones >= (half)0.0f && num_ones <= (half)2.0f);

  // Now num_ones is either 0, 1, or 2. Since 1 and 2 is each
  // represented with a single bit, we can collapse them with
  // a shift and add:
  //   num_ones    output
  //         00         0
  //         01         1
  //         10         1
  Fluint8 nn(num_ones);
  return Fluint8(nn.h + RightShift1(nn).h);
}

Fluint8 Fluint8::IsZero(Fluint8 a) {
  // We know IsZero returns 1 or 0.
  return Fluint8(1.0_h - IsntZero(a).h);
}

void Fluint8::Warm() {
  (void)CanonicalizeExp();
  (void)BitExps();
  (void)Fluint8::Plus(Fluint8(1), Fluint8(2));
  (void)Fluint8::Minus(Fluint8(3), Fluint8(4));
}

#endif
