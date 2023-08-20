
// C++ wrappers for big integers and rationals by tom7 for cc-lib.

#ifndef _CC_LIB_BIGNUM_BIG_H
#define _CC_LIB_BIGNUM_BIG_H

#ifdef BIG_USE_GMP
# include <gmp.h>
#else
# include "bignum/bigz.h"
# include "bignum/bign.h"
# include "bignum/bigq.h"
#endif

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

struct BigInt {
  BigInt() : BigInt(0LL) {}
  inline explicit BigInt(int64_t n);
  inline explicit BigInt(const std::string &digits);

  // Value semantics with linear-time copies (like std::vector).
  inline BigInt(const BigInt &other);
  inline BigInt &operator =(const BigInt &other);
  inline BigInt &operator =(BigInt &&other);

  inline ~BigInt();

  static inline BigInt FromU64(uint64_t u);

  // Aborts if the string is not valid.
  // Bases from [2, 62] are permitted.
  inline std::string ToString(int base = 10) const;

  inline bool IsEven() const;
  inline bool IsOdd() const;

  inline static BigInt Negate(const BigInt &a);
  inline static BigInt Negate(BigInt &&a);
  inline static BigInt Abs(const BigInt &a);
  inline static int Compare(const BigInt &a, const BigInt &b);
  inline static bool Less(const BigInt &a, const BigInt &b);
  inline static bool LessEq(const BigInt &a, const BigInt &b);
  inline static bool Eq(const BigInt &a, const BigInt &b);
  inline static bool Eq(const BigInt &a, int64_t b);
  inline static bool Greater(const BigInt &a, const BigInt &b);
  inline static bool GreaterEq(const BigInt &a, const BigInt &b);
  inline static BigInt Plus(const BigInt &a, const BigInt &b);
  inline static BigInt Plus(const BigInt &a, int64_t b);
  inline static BigInt Minus(const BigInt &a, const BigInt &b);
  inline static BigInt Times(const BigInt &a, const BigInt &b);
  inline static BigInt Times(const BigInt &a, int64_t b);
  inline static BigInt Div(const BigInt &a, const BigInt &b);
  inline static BigInt Div(const BigInt &a, int64_t b);
  // Returns a/b, but requires that that a % b == 0 for correctness.
  inline static BigInt DivExact(const BigInt &a, const BigInt &b);
  // Ignores sign of divisor. Result is always non-negative.
  // XXX need to test that bigz version matches this spec.
  inline static BigInt Mod(const BigInt &a, const BigInt &b);
  inline static BigInt Pow(const BigInt &a, uint64_t exponent);
  // Returns Q (a div b), R (a mod b) such that a = b * q + r
  inline static std::pair<BigInt, BigInt> QuotRem(const BigInt &a,
                                                  const BigInt &b);
  // Integer square root, rounding towards zero.
  // Input must be non-negative.
  static BigInt Sqrt(const BigInt &a);
  inline static BigInt GCD(const BigInt &a, const BigInt &b);
  inline static BigInt LeftShift(const BigInt &a, uint64_t bits);

  // TODO: Implement with bigz too. There is a very straightforward
  // implementation.
  #ifdef BIG_USE_GMP
  // Returns (g, s, t) where g is GCD(a, b) and as + bt = g.
  inline static std::tuple<BigInt, BigInt, BigInt>
  ExtendedGCD(const BigInt &a, const BigInt &b);

  // Only when in about -1e300 to 1e300; readily returns +/- inf
  // for large numbers.
  inline double ToDouble() const;

  // Returns the approximate logarithm, base e.
  inline static double NaturalLog(const BigInt &a);

  inline static BigInt BitwiseAnd(const BigInt &a, const BigInt &b);
  #endif

  // Generate a uniform random number in [0, radix).
  // r should return uniformly random uint64s.
  static BigInt RandTo(const std::function<uint64_t()> &r,
                       const BigInt &radix);

  inline std::optional<int64_t> ToInt() const;


  // Factors using trial division (slow!)
  // such that a0^b0 * a1^b1 * ... * an^bn = x,
  // where a0...an are primes in ascending order
  // and bi is >= 1
  //
  // If max_factor is not -1, then the final term may
  // be composite if its factors are all greater than this
  // number.
  //
  // Input must be positive.
  static std::vector<std::pair<BigInt, int>>
  PrimeFactorization(const BigInt &x, int64_t max_factor = -1);

  // Get 64 (or so) bits of the number. Will be equal for equal a, but
  // no particular value is guaranteed. Intended for hash functions.
  inline static uint64_t LowWord(const BigInt &a);

  inline void Swap(BigInt *other);

private:
  friend struct BigRat;
  #ifdef BIG_USE_GMP
  using Rep = mpz_t;
  void SetU64(uint64_t u) {
    // Need to be able to set 4 bytes at a time.
    static_assert(sizeof (unsigned long int) >= 4);
    const uint32_t hi = 0xFFFFFFFF & (u >> 32);
    const uint32_t lo = 0xFFFFFFFF & u;
    mpz_set_ui(rep, hi);
    mpz_mul_2exp(rep, rep, 32);
    mpz_add_ui(rep, rep, lo);
  }
  #else
  // BigZ is a pointer to a bigz struct, which is the
  // header followed by digits.
  using Rep = BigZ;
  // Takes ownership.
  // nullptr token here is just used to distinguish from the version
  // that takes an int64 (would be ambiguous with BigInt(0)).
  explicit BigInt(Rep z, std::nullptr_t token) : rep(z) {}
  #endif

  Rep rep{};
};


struct BigRat {
  // Zero.
  inline BigRat() : BigRat(0LL, 1LL) {}
  inline BigRat(int64_t numer, int64_t denom);
  inline BigRat(int64_t numer);
  inline BigRat(const BigInt &numer, const BigInt &denom);
  inline BigRat(const BigInt &numer);

  inline BigRat(const BigRat &other);
  inline BigRat &operator =(const BigRat &other);
  inline BigRat &operator =(BigRat &&other);

  inline ~BigRat();

  // In base 10.
  inline std::string ToString() const;
  // Only works when the numerator and denominator are small;
  // readily returns nan!
  inline double ToDouble() const;
  // Get the numerator and denominator.
  inline std::pair<BigInt, BigInt> Parts() const;

  inline static int Compare(const BigRat &a, const BigRat &b);
  inline static bool Eq(const BigRat &a, const BigRat &b);
  inline static BigRat Abs(const BigRat &a);
  inline static BigRat Div(const BigRat &a, const BigRat &b);
  inline static BigRat Inverse(const BigRat &a);
  inline static BigRat Times(const BigRat &a, const BigRat &b);
  inline static BigRat Negate(const BigRat &a);
  inline static BigRat Plus(const BigRat &a, const BigRat &b);
  inline static BigRat Minus(const BigRat &a, const BigRat &b);
  inline static BigRat Pow(const BigRat &a, uint64_t exponent);

  inline static BigRat ApproxDouble(double num, int64_t max_denom);

  inline void Swap(BigRat *other);

private:
  #ifdef BIG_USE_GMP
  using Rep = mpq_t;
  #else
  // TODO: This is a pointer to a struct with two BigZs (pointers),
  // so it would probably be much better to just unpack it here.
  // bigq.cc is seemingly set up to do this by redefining some
  // macros in the EXTERNAL_BIGQ_MEMORY section of the header.
  using Rep = BigQ;
  // Takes ownership.
  // Token for disambiguation as above.
  explicit BigRat(Rep q, std::nullptr_t token) : rep(q) {}
  #endif

  Rep rep{};
};

// Implementations follow. These are all light wrappers around
// the functions in the underlying representation, so inline makes
// sense.

inline BigInt BigInt::FromU64(uint64_t u) {
  uint32_t hi = (u >> 32) & 0xFFFFFFFF;
  uint32_t lo = u         & 0xFFFFFFFF;
  return BigInt::Plus(LeftShift(BigInt{hi}, 32), BigInt{lo});
}

#if BIG_USE_GMP

namespace internal {
inline bool FitsLongInt(int64_t x) {
  return (std::numeric_limits<long int>::min() <= x &&
          x <= std::numeric_limits<long int>::max());
}
}

BigInt::BigInt(int64_t n) {
  mpz_init(rep);
  if (n < 0) {
    SetU64((uint64_t)-n);
    mpz_neg(rep, rep);
  } else {
    SetU64((uint64_t)n);
  }
}

BigInt::BigInt(const BigInt &other) {
  mpz_init(rep);
  mpz_set(rep, other.rep);
}

BigInt &BigInt::operator =(const BigInt &other) {
  // Self-assignment does nothing.
  if (this == &other) return *this;
  mpz_set(rep, other.rep);
  return *this;
}
BigInt &BigInt::operator =(BigInt &&other) {
  // We don't care how we leave other, but it needs to be valid (e.g. for
  // the destructor). Swap is a good way to do this.
  mpz_swap(rep, other.rep);
  return *this;
}

BigInt::BigInt(const std::string &digits) {
  mpz_init(rep);
  int res = mpz_set_str(rep, digits.c_str(), 10);
  if (0 != res) {
    printf("Invalid number [%s]\n", digits.c_str());
    assert(false);
  }
}

BigInt::~BigInt() {
  mpz_clear(rep);
}

void BigInt::Swap(BigInt *other) {
  mpz_swap(rep, other->rep);
}

uint64_t BigInt::LowWord(const BigInt &a) {
  size_t limbs = mpz_size(a.rep);
  if (limbs == 0) return 0;
  // limb 0 is the least significant.
  // XXX if mp_limb_t is not 64 bits, we could get more
  // limbs here.
  return mpz_getlimbn(a.rep, 0);
}

std::string BigInt::ToString(int base) const {
  std::string s;
  // We allocate the space directly in the string to avoid
  // copying.
  // May need space for a minus sign. This function also writes
  // a nul terminating byte, but we don't need that for std::string.
  size_t min_size = mpz_sizeinbase(rep, base);
  s.resize(min_size + 2);
  mpz_get_str(s.data(), base, rep);

  // Now we have a nul-terminated string in the buffer, which is at
  // least one byte too large. We could just use strlen here but
  // we know it's at least min_size.
  for (size_t sz = min_size; sz < s.size(); sz++) {
    if (s[sz] == 0) {
      s.resize(sz);
      return s;
    }
  }
  assert(false);
  return s;
}

double BigInt::ToDouble() const {
  return mpz_get_d(rep);
}

std::optional<int64_t> BigInt::ToInt() const {
  // Get the number of bits, ignoring sign.
  if (mpz_sizeinbase(rep, 2) > 63) {
    return std::nullopt;
  } else {
    // "buffer" where result is written
    uint64_t digit = 0;
    size_t count = 0;
    mpz_export(&digit, &count,
               // order doesn't matter, because there is just one word
               1,
               // 8 bytes
               8,
               // native endianness
               0,
               // 0 "nails" (leading bits to skip)
               0,
               rep);

    assert(count <= 1);
    assert (!(digit & 0x8000000000000000ULL));
    if (mpz_sgn(rep) == -1) {
      return {-(int64_t)digit};
    }
    return {(int64_t)digit};
  }
}


double BigInt::NaturalLog(const BigInt &a) {
  // d is the magnitude, with absolute value in [0.5,1].
  //   a = di * 2^exponent
  // taking the log of both sides,
  //   log(a) = log(di) + log(2) * exponent
  signed long int exponent = 0;
  const double di = mpz_get_d_2exp(&exponent, a.rep);
  return std::log(di) + std::log(2.0) * (double)exponent;
}

BigInt BigInt::BitwiseAnd(const BigInt &a, const BigInt &b) {
  BigInt ret;
  mpz_and(ret.rep, a.rep, b.rep);
  return ret;
}

bool BigInt::IsEven() const {
  return mpz_even_p(rep);
}
bool BigInt::IsOdd() const {
  return mpz_odd_p(rep);
}

BigInt BigInt::Negate(const BigInt &a) {
  BigInt ret;
  mpz_neg(ret.rep, a.rep);
  return ret;
}
BigInt BigInt::Negate(BigInt &&a) {
  mpz_neg(a.rep, a.rep);
  return a;
}

BigInt BigInt::Abs(const BigInt &a) {
  BigInt ret;
  mpz_abs(ret.rep, a.rep);
  return ret;
}
int BigInt::Compare(const BigInt &a, const BigInt &b) {
  int r = mpz_cmp(a.rep, b.rep);
  if (r < 0) return -1;
  else if (r > 0) return 1;
  else return 0;
}
bool BigInt::Less(const BigInt &a, const BigInt &b) {
  return mpz_cmp(a.rep, b.rep) < 0;
}
bool BigInt::LessEq(const BigInt &a, const BigInt &b) {
  return mpz_cmp(a.rep, b.rep) <= 0;
}
bool BigInt::Eq(const BigInt &a, const BigInt &b) {
  return mpz_cmp(a.rep, b.rep) == 0;
}
bool BigInt::Eq(const BigInt &a, int64_t b) {
  if (internal::FitsLongInt(b)) {
    signed long int sb = b;
    return mpz_cmp_si(a.rep, sb) == 0;
  } else {
    return Eq(a, BigInt(b));
  }
}

bool BigInt::Greater(const BigInt &a, const BigInt &b) {
  return mpz_cmp(a.rep, b.rep) > 0;
}
bool BigInt::GreaterEq(const BigInt &a, const BigInt &b) {
  return mpz_cmp(a.rep, b.rep) >= 0;
}

BigInt BigInt::Plus(const BigInt &a, const BigInt &b) {
  BigInt ret;
  mpz_add(ret.rep, a.rep, b.rep);
  return ret;
}

BigInt BigInt::Plus(const BigInt &a, int64_t b) {
  // PERF could also support negative b. but GMP only has
  // _ui version.
  if (b >= 0 && internal::FitsLongInt(b)) {
    signed long int sb = b;
    BigInt ret;
    mpz_add_ui(ret.rep, a.rep, sb);
    return ret;
  } else {
    return Plus(a, BigInt(b));
  }
}

BigInt BigInt::Minus(const BigInt &a, const BigInt &b) {
  BigInt ret;
  mpz_sub(ret.rep, a.rep, b.rep);
  return ret;
}

BigInt BigInt::Times(const BigInt &a, const BigInt &b) {
  BigInt ret;
  mpz_mul(ret.rep, a.rep, b.rep);
  return ret;
}

BigInt BigInt::Times(const BigInt &a, int64_t b) {
  if (internal::FitsLongInt(b)) {
    signed long int sb = b;
    BigInt ret;
    mpz_mul_si(ret.rep, a.rep, sb);
    return ret;
  } else {
    return Times(a, BigInt(b));
  }
}

BigInt BigInt::Div(const BigInt &a, const BigInt &b) {
  // truncate (round towards zero) like C
  BigInt ret;
  mpz_tdiv_q(ret.rep, a.rep, b.rep);
  return ret;
}

BigInt BigInt::Div(const BigInt &a, int64_t b) {
  if (internal::FitsLongInt(b)) {
    // alas there is no _si version, so branch on
    // the sign.
    if (b >= 0) {
      signed long int sb = b;
      BigInt ret;
      mpz_tdiv_q_ui(ret.rep, a.rep, sb);
      return ret;
    } else {
      signed long int sb = -b;
      BigInt ret;
      mpz_tdiv_q_ui(ret.rep, a.rep, sb);
      mpz_neg(ret.rep, ret.rep);
      return ret;
    }
  } else {
    return Div(a, BigInt(b));
  }
}

BigInt BigInt::DivExact(const BigInt &a, const BigInt &b) {
  BigInt ret;
  mpz_divexact(ret.rep, a.rep, b.rep);
  return ret;
}


BigInt BigInt::Mod(const BigInt &a, const BigInt &b) {
  BigInt ret;
  mpz_mod(ret.rep, a.rep, b.rep);
  return ret;
}

// Returns Q (a div b), R (a mod b) such that a = b * q + r
std::pair<BigInt, BigInt> BigInt::QuotRem(const BigInt &a,
                                          const BigInt &b) {
  BigInt q, r;
  mpz_tdiv_qr(q.rep, r.rep, a.rep, b.rep);
  return std::make_pair(q, r);
}

BigInt BigInt::Pow(const BigInt &a, uint64_t exponent) {
  BigInt ret;
  mpz_pow_ui(ret.rep, a.rep, exponent);
  return ret;
}

BigInt BigInt::LeftShift(const BigInt &a, uint64_t shift) {
  if (internal::FitsLongInt(shift)) {
    mp_bitcnt_t sh = shift;
    BigInt ret;
    mpz_mul_2exp(ret.rep, a.rep, sh);
    return ret;
  } else {
    return Times(a, Pow(BigInt{2}, shift));
  }
}

BigInt BigInt::GCD(const BigInt &a, const BigInt &b) {
  BigInt ret;
  mpz_gcd(ret.rep, a.rep, b.rep);
  return ret;
}

std::tuple<BigInt, BigInt, BigInt>
BigInt::ExtendedGCD(const BigInt &a, const BigInt &b) {
  BigInt g, s, t;
  mpz_gcdext(g.rep, s.rep, t.rep, a.rep, b.rep);
  return std::make_tuple(g, s, t);
}

BigRat::BigRat(int64_t numer, int64_t denom) :
  BigRat(BigInt{numer}, BigInt{denom}) {}

BigRat::BigRat(int64_t numer) : BigRat(BigInt{numer}) {}

BigRat::BigRat(const BigInt &numer, const BigInt &denom) {
  mpq_init(rep);
  mpq_set_z(rep, numer.rep);

  Rep tmp;
  mpq_init(tmp);
  mpq_set_z(tmp, denom.rep);
  mpq_div(rep, rep, tmp);
  mpq_clear(tmp);
}

BigRat::BigRat(const BigInt &numer) {
  BigInt n{numer};
  mpq_init(rep);
  mpq_set_z(rep, numer.rep);
}

BigRat::BigRat(const BigRat &other) {
  mpq_init(rep);
  mpq_set(rep, other.rep);
}
BigRat &BigRat::operator =(const BigRat &other) {
  // Self-assignment does nothing.
  if (this == &other) return *this;
  mpq_set(rep, other.rep);
  return *this;
}
BigRat &BigRat::operator =(BigRat &&other) {
  Swap(&other);
  return *this;
}

void BigRat::Swap(BigRat *other) {
  mpq_swap(rep, other->rep);
}

BigRat::~BigRat() {
  mpq_clear(rep);
}

int BigRat::Compare(const BigRat &a, const BigRat &b) {
  const int r = mpq_cmp(a.rep, b.rep);
  if (r < 0) return -1;
  else if (r > 0) return 1;
  else return 0;
}

bool BigRat::Eq(const BigRat &a, const BigRat &b) {
  return mpq_cmp(a.rep, b.rep) == 0;
}

BigRat BigRat::Abs(const BigRat &a) {
  BigRat ret;
  mpq_abs(ret.rep, a.rep);
  return ret;
}
BigRat BigRat::Div(const BigRat &a, const BigRat &b) {
  BigRat ret;
  mpq_div(ret.rep, a.rep, b.rep);
  return ret;
}
BigRat BigRat::Inverse(const BigRat &a) {
  BigRat ret;
  mpq_inv(ret.rep, a.rep);
  return ret;
}
BigRat BigRat::Times(const BigRat &a, const BigRat &b) {
  BigRat ret;
  mpq_mul(ret.rep, a.rep, b.rep);
  return ret;
}
BigRat BigRat::Negate(const BigRat &a) {
  BigRat ret;
  mpq_neg(ret.rep, a.rep);
  return ret;
}
BigRat BigRat::Plus(const BigRat &a, const BigRat &b) {
  BigRat ret;
  mpq_add(ret.rep, a.rep, b.rep);
  return ret;
}
BigRat BigRat::Minus(const BigRat &a, const BigRat &b) {
  BigRat ret;
  mpq_sub(ret.rep, a.rep, b.rep);
  return ret;
}

std::string BigRat::ToString() const {
  const auto &[numer, denom] = Parts();
  std::string ns = numer.ToString();
  if (mpz_cmp_ui(denom.rep, 1)) {
    // for n/1
    return ns;
  } else {
    std::string ds = denom.ToString();
    return ns + "/" + ds;
  }
}

std::pair<BigInt, BigInt> BigRat::Parts() const {
  BigInt numer, denom;
  mpz_set(numer.rep, mpq_numref(rep));
  mpz_set(denom.rep, mpq_denref(rep));
  return std::make_pair(numer, denom);
}

BigRat BigRat::ApproxDouble(double num, int64_t max_denom) {
  // XXX implement max_denom somehow?
  BigRat ret;
  mpq_set_d(ret.rep, num);
  return ret;
}

double BigRat::ToDouble() const {
  return mpq_get_d(rep);
}

#else
// No GMP. Using portable big*.h.


BigInt::BigInt(int64_t n) : rep(BzFromInteger(n)) { }

BigInt::BigInt(const BigInt &other) : rep(BzCopy(other.rep)) { }
BigInt &BigInt::operator =(const BigInt &other) {
  // Self-assignment does nothing.
  if (this == &other) return *this;
  BzFree(rep);
  rep = BzCopy(other.rep);
  return *this;
}
BigInt &BigInt::operator =(BigInt &&other) {
  // We don't care how we leave other, but it needs to be valid (e.g. for
  // the destructor). Swap is a good way to do this.
  Swap(&other);
  return *this;
}

BigInt::~BigInt() {
  BzFree(rep);
  rep = nullptr;
}

void BigInt::Swap(BigInt *other) {
  std::swap(rep, other->rep);
}

BigInt::BigInt(const std::string &digits) {
  rep = BzFromStringLen(digits.c_str(), digits.size(), 10, BZ_UNTIL_END);
}

std::string BigInt::ToString(int base) const {
  // Allocates a buffer.
  // Third argument forces a + sign for positive; not used here.
  BzChar *buf = BzToString(rep, base, 0);
  std::string ret{buf};
  BzFreeString(buf);
  return ret;
}

std::optional<int64_t> BigInt::ToInt() const {
  if (BzNumDigits(rep) > (BigNumLength)1) {
    return std::nullopt;
  } else {
    uint64_t digit = BzGetDigit(rep, 0);
    // Would overflow int64. (This may be a bug in BzToInteger?)
    if (digit & 0x8000000000000000ULL)
      return std::nullopt;
    if (BzGetSign(rep) == BZ_MINUS) {
      return {-(int64_t)digit};
    }
    return {(int64_t)digit};
  }
}

uint64_t BigInt::LowWord(const BigInt &a) {
  if (BzNumDigits(a.rep) == 0) return uint64_t{0};
  else return BzGetDigit(a.rep, 0);
}

bool BigInt::IsEven() const { return BzIsEven(rep); }
bool BigInt::IsOdd() const { return BzIsOdd(rep); }

BigInt BigInt::Negate(const BigInt &a) {
  return BigInt{BzNegate(a.rep), nullptr};
}
BigInt BigInt::Negate(BigInt &&a) {
  // PERF any way to negate in place?
  return BigInt{BzNegate(a.rep), nullptr};
}

BigInt BigInt::Abs(const BigInt &a) {
  return BigInt{BzAbs(a.rep), nullptr};
}
int BigInt::Compare(const BigInt &a, const BigInt &b) {
  switch (BzCompare(a.rep, b.rep)) {
  case BZ_LT: return -1;
  case BZ_EQ: return 0;
  default:
  case BZ_GT: return 1;
  }
}
bool BigInt::Less(const BigInt &a, const BigInt &b) {
  return BzCompare(a.rep, b.rep) == BZ_LT;
}
bool BigInt::LessEq(const BigInt &a, const BigInt &b) {
  auto cmp = BzCompare(a.rep, b.rep);
  return cmp == BZ_LT || cmp == BZ_EQ;
}
bool BigInt::Eq(const BigInt &a, const BigInt &b) {
  return BzCompare(a.rep, b.rep) == BZ_EQ;
}
bool BigInt::Eq(const BigInt &a, int64_t b) {
  return BzCompare(a.rep, BigInt{b, nullptr}) == BZ_EQ;
}

bool BigInt::Greater(const BigInt &a, const BigInt &b) {
  return BzCompare(a.rep, b.rep) == BZ_GT;
}
bool BigInt::GreaterEq(const BigInt &a, const BigInt &b) {
  auto cmp = BzCompare(a.rep, b.rep);
  return cmp == BZ_GT || cmp == BZ_EQ;
}

BigInt BigInt::Plus(const BigInt &a, const BigInt &b) {
  return BigInt{BzAdd(a.rep, b.rep), nullptr};
}
BigInt BigInt::Plus(const BigInt &a, int64_t b) {
  return BigInt{BzAdd(a.rep, BigInt{b, nullptr}.rep), nullptr};
}
BigInt BigInt::Minus(const BigInt &a, const BigInt &b) {
  return BigInt{BzSubtract(a.rep, b.rep), nullptr};
}
BigInt BigInt::Times(const BigInt &a, const BigInt &b) {
  return BigInt{BzMultiply(a.rep, b.rep), nullptr};
}
BigInt BigInt::Times(const BigInt &a, int64_t b) {
  return BigInt{BzMultiply(a.rep, BigInt{b, nullptr}.rep), nullptr};
}

// TODO: Quotrem via BzDivide
BigInt BigInt::Div(const BigInt &a, const BigInt &b) {
  return BigInt{BzDiv(a.rep, b.rep), nullptr};
}
BigInt BigInt::DivExact(const BigInt &a, const BigInt &b) {
  // Not using the precondition here; same as division.
  return BigInt{BzDiv(a.rep, b.rep), nullptr};
}
BigInt BigInt::Times(const BigInt &a, int64_t b) {
  return BigInt{BzDiv(a.rep, BigInt{b, nullptr}.rep), nullptr};
}

// TODO: truncate, floor, ceiling round. what are they?

// TODO: Clarify mod vs rem?
BigInt BigInt::Mod(const BigInt &a, const BigInt &b) {
  return BigInt{BzMod(a.rep, b.rep), nullptr};
}

// Returns Q (a div b), R (a mod b) such that a = b * q + r
std::pair<BigInt, BigInt> BigInt::QuotRem(const BigInt &a,
                                          const BigInt &b) {
  BigZ r;
  BigZ q = BzDivide(a.rep, b.rep, &r);
  return std::make_pair(BigInt{q, nullptr}, BigInt{r, nullptr});
}

BigInt BigInt::Pow(const BigInt &a, uint64_t exponent) {
  return BigInt{BzPow(a.rep, exponent), nullptr};
}

BigInt BigInt::LeftShift(const BigInt &a, uint64_t bits) {
  return Times(a, Pow(BigInt{2, nullptr}, bits));
}

BigInt BigInt::GCD(const BigInt &a, const BigInt &b) {
  return BigInt{BzGcd(a.rep, b.rep), nullptr};
}

BigRat::BigRat(int64_t numer, int64_t denom) {
  // PERF This could avoid creating intermediate BigZ with
  // a new function inside BigQ.
  BigInt n{numer}, d{denom};
  rep = BqCreate(n.rep, d.rep);
}
BigRat::BigRat(int64_t numer) : BigRat(numer, int64_t{1}) {}

BigRat::BigRat(const BigInt &numer, const BigInt &denom)
  : rep(BqCreate(numer.rep, denom.rep)) {}

BigRat::BigRat(const BigInt &numer) : BigRat(numer, BigInt(1)) {}

// PERF: Should have BqCopy so that we don't need to re-normalize.
BigRat::BigRat(const BigRat &other) :
  rep(BqCreate(
           BqGetNumerator(other.rep),
           BqGetDenominator(other.rep))) {
}
BigRat &BigRat::operator =(const BigRat &other) {
  // Self-assignment does nothing.
  if (this == &other) return *this;
  BqDelete(rep);
  rep = BqCreate(BqGetNumerator(other.rep),
                  BqGetDenominator(other.rep));
  return *this;
}
BigRat &BigRat::operator =(BigRat &&other) {
  Swap(&other);
  return *this;
}

void BigRat::Swap(BigRat *other) {
  std::swap(rep, other->rep);
}

BigRat::~BigRat() {
  BqDelete(rep);
  rep = nullptr;
}

int BigRat::Compare(const BigRat &a, const BigRat &b) {
  switch (BqCompare(a.rep, b.rep)) {
  case BQ_LT: return -1;
  case BQ_EQ: return 0;
  default:
  case BQ_GT: return 1;
  }
}

bool BigRat::Eq(const BigRat &a, const BigRat &b) {
  return BqCompare(a.rep, b.rep) == BQ_EQ;
}

BigRat BigRat::Abs(const BigRat &a) {
  return BigRat{BqAbs(a.rep), nullptr};
}
BigRat BigRat::Div(const BigRat &a, const BigRat &b) {
  return BigRat{BqDiv(a.rep, b.rep), nullptr};
}
BigRat BigRat::Inverse(const BigRat &a) {
  return BigRat{BqInverse(a.rep), nullptr};
}
BigRat BigRat::Times(const BigRat &a, const BigRat &b) {
  return BigRat{BqMultiply(a.rep, b.rep), nullptr};
}
BigRat BigRat::Negate(const BigRat &a) {
  return BigRat{BqNegate(a.rep), nullptr};
}
BigRat BigRat::Plus(const BigRat &a, const BigRat &b) {
  Rep res = BqAdd(a.rep, b.rep);
  return BigRat{res, nullptr};
}
BigRat BigRat::Minus(const BigRat &a, const BigRat &b) {
  return BigRat{BqSubtract(a.rep, b.rep), nullptr};
}

std::string BigRat::ToString() const {
  // No forced +
  BzChar *buf = BqToString(rep, 0);
  std::string ret{buf};
  BzFreeString(buf);
  return ret;
}

std::pair<BigInt, BigInt> BigRat::Parts() const {
  return std::make_pair(BigInt(BzCopy(BqGetNumerator(rep)), nullptr),
                        BigInt(BzCopy(BqGetDenominator(rep)), nullptr));
}

BigRat BigRat::ApproxDouble(double num, int64_t max_denom) {
  return BigRat{BqFromDouble(num, max_denom), nullptr};
}

double BigRat::ToDouble() const {
  return BqToDouble(rep);
}

#endif

// Common / derived implementations.

BigRat BigRat::Pow(const BigRat &a, uint64_t exponent) {
  const auto &[numer, denom] = a.Parts();
  BigInt nn(BigInt::Pow(numer, exponent));
  BigInt dd(BigInt::Pow(denom, exponent));
  return BigRat(nn, dd);
}


#endif
