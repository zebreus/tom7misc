
// C++ wrappers for big integers and rationals by tom7 for cc-lib.

#ifndef _CC_LIB_BIGNUM_BIG_H
#define _CC_LIB_BIGNUM_BIG_H

#include <cstdlib>
#ifdef BIG_USE_GMP
# include <gmp.h>
# include "bignum/wrap-gmp.h"
#else
# include "bignum/bigz.h"
# include "bignum/bign.h"
# include "bignum/bigq.h"
#endif

#include <span>
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "base/logging.h"

struct BigInt {
  static_assert(std::integral<size_t>);

  BigInt() : BigInt(uint32_t{0}) {}
  // From any integral type, but only up to 64 bits are supported.
  inline explicit BigInt(std::integral auto n);
  inline explicit BigInt(std::string_view digits);

  // Value semantics with linear-time copies (like std::vector).
  inline BigInt(const BigInt &other);
  inline BigInt(BigInt &&other) noexcept;
  inline BigInt &operator =(const BigInt &other);
  inline BigInt &operator =(BigInt &&other) noexcept;

  inline ~BigInt();

  static inline BigInt FromU64(uint64_t u);
  // Big-endian, unsigned.
  static BigInt FromBigEndianBytes(std::span<const uint8_t> bytes);

  // TODO: From doubles (rounding), which is useful because
  // uint64_t can't represent all large doubles.

  // Aborts if the string is not valid.
  // Bases from [2, 62] are permitted.
  inline std::string ToString(int base = 10) const;

  inline bool IsEven() const;
  inline bool IsOdd() const;

  // Returns -1, 0, or 1.
  inline static int Sign(const BigInt &a);
  inline static bool IsZero(const BigInt &a);

  inline static BigInt Negate(const BigInt &a);
  inline static BigInt Negate(BigInt &&a);
  inline static BigInt Abs(const BigInt &a);
  inline static BigInt Abs(BigInt &&a);
  inline static int Compare(const BigInt &a, const BigInt &b);
  inline static int Compare(const BigInt &a, int64_t b);
  inline static bool Less(const BigInt &a, const BigInt &b);
  inline static bool Less(const BigInt &a, int64_t b);
  inline static bool LessEq(const BigInt &a, const BigInt &b);
  inline static bool LessEq(const BigInt &a, int64_t b);
  inline static bool Eq(const BigInt &a, const BigInt &b);
  inline static bool Eq(const BigInt &a, int64_t b);
  inline static bool Greater(const BigInt &a, const BigInt &b);
  inline static bool Greater(const BigInt &a, int64_t b);
  inline static bool GreaterEq(const BigInt &a, const BigInt &b);
  inline static bool GreaterEq(const BigInt &a, int64_t b);

  inline static BigInt Plus(const BigInt &a, const BigInt &b);
  inline static void PlusEq(BigInt &a, const BigInt &b);
  inline static BigInt Minus(const BigInt &a, const BigInt &b);
  inline static BigInt Times(const BigInt &a, const BigInt &b);

  // PERF: Great place to support && args
  inline static BigInt Min(const BigInt &a, const BigInt &b);
  inline static BigInt Max(const BigInt &a, const BigInt &b);

  // Truncates towards zero, like C.
  inline static BigInt Div(const BigInt &a, const BigInt &b);
  inline static BigInt Div(const BigInt &a, int64_t b);

  // Rounds towards negative infinity.
  inline static BigInt DivFloor(const BigInt &a, const BigInt &b);
  inline static BigInt DivFloor(const BigInt &a, int64_t b);

  // Equivalent to num % den == 0; maybe faster.
  inline static bool DivisibleBy(const BigInt &num, const BigInt &den);
  inline static bool DivisibleBy(const BigInt &num, int64_t den);
  // Returns a/b, but requires that that a % b == 0 for correctness.
  inline static BigInt DivExact(const BigInt &a, const BigInt &b);
  inline static BigInt DivExact(const BigInt &a, int64_t b);


  // TODO: Check that the behavior on negative numbers is the
  // same between the GMP and bignum implementations.

  // Ignores sign of b. Result is always in [0, |b|).
  // For the C % operator, use CMod.
  inline static BigInt Mod(const BigInt &a, const BigInt &b);
  // TODO: Could offer uint64_t Mod, returning uint64_t.

  // Modulus with C99/C++11 semantics: Division truncates towards
  // zero; modulus has the same sign as a.
  // cmod(a, b) = a - trunc(a / b) * b
  inline static BigInt CMod(const BigInt &a, const BigInt &b);
  inline static int64_t CMod(const BigInt &a, int64_t b);

  // Returns Q (a div b), R (a mod b) such that a = b * q + r
  // This is Div(a, b) and CMod(a, b); a / b and a % b in C.
  inline static std::pair<BigInt, BigInt> QuotRem(const BigInt &a,
                                                  const BigInt &b);

  inline static BigInt Pow(const BigInt &a, uint64_t exponent);

  // Integer square root, rounding towards zero.
  // Input must be non-negative.
  inline static BigInt Sqrt(const BigInt &a);
  // Returns a = floor(sqrt(aa)) and aa - a^2.
  inline static std::pair<BigInt, BigInt> SqrtRem(const BigInt &aa);
  inline static BigInt GCD(const BigInt &a, const BigInt &b);
  // Always non-negative. Returns zero if either argument is zero.
  static BigInt LCM(const BigInt &a, const BigInt &b);

  inline static BigInt LeftShift(const BigInt &a, uint64_t bits);
  inline static BigInt RightShift(const BigInt &a, uint64_t bits);
  inline static BigInt BitwiseAnd(const BigInt &a, const BigInt &b);
  inline static uint64_t BitwiseAnd(const BigInt &a, uint64_t b);

  inline static BigInt BitwiseXor(const BigInt &a, const BigInt &b);
  inline static BigInt BitwiseOr(const BigInt &a, const BigInt &b);
  // Return the number of trailing zeroes. For an input of zero,
  // this is zero (this differs from std::countr_zero<T>, which returns
  // the finite size of T in bits for zero).
  inline static uint64_t BitwiseCtz(const BigInt &a);

  // Only when in about -1e300 to 1e300; readily returns +/- inf
  // for large numbers.
  inline double ToDouble() const;

  // Compute (base^exp) % mod.
  static BigInt PowMod(const BigInt &base, const BigInt &exp,
                       const BigInt &mod);

  // Returns (g, s, t) where g is GCD(a, b) and as + bt = g.
  // ("Bézout coefficients" where |s| <= |b/d| and |y| <= |a/d|).
  inline static std::tuple<BigInt, BigInt, BigInt>
  ExtendedGCD(const BigInt &a, const BigInt &b);

  // Compute the modular inverse of a mod b. Returns nullopt if
  // it does not exist.
  inline static std::optional<BigInt> ModInverse(
      const BigInt &a, const BigInt &b);

  // Returns the approximate logarithm, base e.
  // Note that functions using double like this are inexact and
  // might differ between GMP and non-GMP.
  inline static double NaturalLog(const BigInt &a);
  inline static double LogBase2(const BigInt &a);

  // The number of bits in the number. The sign is ignored.
  // Zero is considered to have zero bits.
  inline static size_t NumBits(const BigInt &a);

  // Jacobi symbol (-1, 0, 1). b must be odd.
  inline static int Jacobi(const BigInt &a, const BigInt &b);

  // Generate a uniform random number in [0, radix).
  // r should return uniformly random uint64s.
  static BigInt RandTo(const std::function<uint64_t()> &r,
                       const BigInt &radix);

  inline std::optional<int64_t> ToInt() const;

  // Returns nullopt for negative numbers, or numbers larger
  // than 2^64-1.
  inline std::optional<uint64_t> ToU64() const;

  // Compute the prime factorization of x,
  // such that a0^b0 * a1^b1 * ... * an^bn = x,
  // where a0...an are primes in ascending order
  // and bi is >= 1.
  // This will be empty for x = 1.
  //
  // If max_factor is not -1, then the final term may
  // be composite if its factors are all greater than this
  // number. (This argument is currently ignored, though.)
  //
  // Input must be positive.
  static std::vector<std::pair<BigInt, int>>
  PrimeFactorization(const BigInt &x, int64_t max_factor = -1);

  // Exact primality test. Numbers less than 2 are considered to be not
  // prime.
  static bool IsPrime(const BigInt &x);

  // Miller-Rabin probabilistic primality test. If this returns false,
  // it is definitely not prime. If it returns true, the probability
  // of being prime is about 1 - 1/(2^(num_steps * 2)). Note that this
  // uses pseduorandom bases for the test, so it is not strong against
  // an adversary.
  static bool IsProbablyPrime(const BigInt &x, int num_steps = 64);

  // Get 64 (or so) bits of the number. Will be equal for equal a, but
  // no particular value is guaranteed. Not stable between backends or
  // versions. Intended for hash functions.
  inline static uint64_t HashCode(const BigInt &a);

  inline void Swap(BigInt *other);


 private:
  friend struct BigRat;

  #ifdef BIG_USE_GMP

  using Rep = GmpRep;

  static void MpzSetU64(MP_INT *mpz, uint64_t u) {
    // Need to be able to set 4 bytes at a time.
    static_assert(sizeof (unsigned long int) >= 4);
    const uint32_t hi = 0xFFFFFFFF & (u >> 32);
    const uint32_t lo = 0xFFFFFFFF & u;
    mpz_set_ui(mpz, hi);
    mpz_mul_2exp(mpz, mpz, 32);
    mpz_add_ui(mpz, mpz, lo);
  }

  // XXX figure out how to hide this stuff away.
  // Could also move this to a big-util or whatever.
  static void FactorUsingDivision(
      mpz_t, std::vector<std::pair<BigInt, int>> *);
  static std::vector<std::pair<BigInt, int>>
  PrimeFactorizationInternal(mpz_t x);
  static void FactorUsingPollardRho(
      mpz_t n, unsigned long a,
      std::vector<std::pair<BigInt, int>> *factors);
  static bool MpzIsPrime(const mpz_t n);
  static void InsertFactorMPZ(std::vector<std::pair<BigInt, int>> *factors,
                              mpz_t p,
                              unsigned int exponent);

  #else
  // BigZ is a pointer to a bigz struct, which is the
  // header followed by digits.
  using Rep = BigZ;
  // Takes ownership.
  // nullptr token here is just used to distinguish from the version
  // that takes an int64 (would be ambiguous with BigInt(0)).
  explicit BigInt(Rep z, std::nullptr_t token) : rep(z) {}

  static double LogBase2Internal(const BigInt &a);

  #endif

 public:
  // Not recommended! And inherently not portable between
  // representations. But for example you can use this to efficiently
  // create BigInts from arrays of words using mpz_import.
  // (Speaking of which: Rep changed from mpz_t to GmpRep, which
  // you need to call GmpRep::MPZ() in order to access the underlying
  // words.)
  Rep &GetRep() { return rep; }
  const Rep &GetRep() const { return rep; }

 private:
  Rep rep{};
};


struct BigRat {
  // Zero.
  inline BigRat() : BigRat(0LL, 1LL) {}
  inline explicit BigRat(int64_t numer, int64_t denom);
  inline explicit BigRat(int64_t numer);
  // "-123" and "-123/567" are supported.
  explicit BigRat(std::string_view s);
  inline BigRat(const BigInt &numer, const BigInt &denom);
  inline BigRat(const BigInt &numer);
  // PERF: Could have versions that move numerator/denominator?

  inline BigRat(const BigRat &other);
  inline BigRat(BigRat &&other) noexcept;
  inline BigRat &operator =(const BigRat &other);
  inline BigRat &operator =(BigRat &&other) noexcept;

  // Must be of the form [-]digits[.digits]; no exponential notation.
  // The finite decimal expansion is represented exactly.
  static BigRat FromDecimal(std::string_view num);

  inline static BigRat FromDouble(double num);
  inline static BigRat ApproxDouble(double num, int64_t max_denom);

  inline ~BigRat();

  // In base 10, as either an integer (with denominator = 1) or "n/d".
  inline std::string ToString() const;
  // Should get the result within one ULP for all representable
  // doubles. Returns positive or negative infinity if the value is
  // too large. Without GMP, not very efficient (does binary search).
  inline double ToDouble() const;
  // Get the numerator and denominator.
  // For negative rationals, the numerator will be the negative one.
  inline std::pair<BigInt, BigInt> Parts() const;
  inline BigInt Denominator() const;
  inline BigInt Numerator() const;

  // Get 64 bits from both the numerator and denominator. Will be the
  // same for equal inputs, but has no other meaning. Might not be
  // stable between backends or across versions. Intended for hash
  // functions.
  inline static uint64_t HashCode(const BigRat &a);

  // Returns -1, 0, or 1.
  inline static int Sign(const BigRat &a);
  // Tests for zero using the sign. Avoids constructing BigRat(0).
  inline static bool IsZero(const BigRat &a);

  // Returns -1, 0, or 1.
  // Note that if you just want to check equality, Eq is faster.
  inline static int Compare(const BigRat &a, const BigRat &b);
  inline static int Compare(const BigRat &a, const BigInt &b);
  inline static bool Eq(const BigRat &a, const BigRat &b);
  inline static bool Eq(const BigRat &a, int64_t b);
  inline static bool Less(const BigRat &a, const BigRat &b);
  inline static bool LessEq(const BigRat &a, const BigRat &b);
  inline static bool Greater(const BigRat &a, const BigRat &b);
  inline static bool GreaterEq(const BigRat &a, const BigRat &b);

  inline static BigRat Abs(const BigRat &a);
  inline static BigRat Div(const BigRat &a, const BigRat &b);
  // The reciprocal, 1/r.
  inline static BigRat Inverse(const BigRat &a);
  inline static BigRat Times(const BigRat &a, const BigRat &b);
  inline static BigRat Negate(const BigRat &a);
  inline static BigRat Negate(BigRat &&a);
  inline static BigRat Plus(const BigRat &a, const BigRat &b);
  inline static BigRat Minus(const BigRat &a, const BigRat &b);
  inline static BigRat Pow(const BigRat &a, uint64_t exponent);
  inline static BigRat Min(const BigRat &a, const BigRat &b);
  inline static BigRat Max(const BigRat &a, const BigRat &b);

  // Compute the largest integer less than or equal to r.
  inline static BigInt Floor(const BigRat &r);
  // And the smallest integer greater than or equal to r.
  inline static BigInt Ceil(const BigRat &r);

  // Returns a good rational approximation to the square root of a,
  // with a denominator of no larger than inv_epsilon.
  static BigRat Sqrt(const BigRat &a, const BigInt &inv_epsilon);
  // Same, for cube root.
  static BigRat Cbrt(const BigRat &a, const BigInt &inv_epsilon);


  // Return accurate bounds on the square root of a,
  // with a denominator no larger than inv_epsilon and where
  // the width of the interval is no more than 1/inv_epsilon.
  static std::pair<BigRat, BigRat> SqrtBounds(const BigRat &a,
                                              const BigInt &inv_epsilon);

  // Considering only rationals with denominator <= inv_epsilon,
  // find lb <= a <= ub such that lb and ub are as close as possible
  // to a. The distance between lb and ub is <= 1/inv_epsilon.
  // Formally they are Farey neighbors in F_{inv_epsilon}, and
  // we call this an elementary interval.
  // It works by using the convergents of the continued fraction,
  // but also the semiconvergents.
  static std::pair<BigRat, BigRat>
  ElementaryBounds(const BigRat &a, const BigInt &inv_epsilon);

  // Turns a high precision interval into a high quality one. The
  // input [lb, ub] must have width no more than 1/(inv_epsilon^2).
  // Computes new lower and upper bounds that contain the original
  // interval, and use denominators no more than inv_epsilon. These
  // are the first and last components of the returned triple. If the
  // middle element is absent, then the new bounds are Farey neighbors
  // in F_{inv_epsilon} and are thus no more than 1/inv_epsilon apart.
  // If the middle element is present, then this is a Farey triple
  // (lb, s, ub), where s is a simple fraction (often much simpler)
  // that falls in the interval. In this case, the full interval may
  // be up to 2/inv_epsilon in width. You may be able to determine one
  // of the two sub-intervals to choose with further tests (and
  // heuristically, the middle element may be the exact rational
  // answer you are trying to approximate).
  static std::tuple<BigRat, std::optional<BigRat>, BigRat>
  SimplifyInterval(const BigRat &lb, const BigRat &ub,
                   const BigInt &inv_epsilon);

  // Truncate 'a' to a good rational approximation (with denominator
  // no larger than inv_epsilon), using only convergents of the
  // continued fraction. This is a little faster than
  // ElementaryBounds, but does not necessarily produce the best
  // approximation, and the error may be more than 1/inv_epsilon! An
  // appropriate use would be to reduce precision during iterative
  // approximations, like those used above.
  static BigRat Truncate(const BigRat &a, const BigInt &inv_epsilon);

  inline void Swap(BigRat *other);

 private:
  // Doesn't work?
  // template <typename T, typename U>
  // requires std::floating_point<T> && std::floating_point<U>
  // BigRat(T, U) = delete;

  #ifdef BIG_USE_GMP
  using Rep = GmpRepRat;

  #else
  // TODO: This is a pointer to a struct with two BigZs (pointers),
  // so it would probably be much better to just unpack it here.
  // bigq.cc is seemingly set up to do this by redefining some
  // macros in the EXTERNAL_BIGQ_MEMORY section of the header.
  using Rep = BigQ;
  // Takes ownership.
  // Token for disambiguation as above.
  explicit BigRat(Rep q, std::nullptr_t token) : rep(q) {}
  static double ToDoubleIterative(const BigRat &r);
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

inline BigInt BigInt::Max(const BigInt &a, const BigInt &b) {
  return BigInt::Less(a, b) ? b : a;
}

inline BigInt BigInt::Min(const BigInt &a, const BigInt &b) {
  return BigInt::Less(a, b) ? a : b;
}

#if BIG_USE_GMP

namespace internal {
inline constexpr bool FitsSignedLong(int64_t x) {
  return std::in_range<signed long int>(x);
}
inline constexpr bool FitsUnsignedLong(int64_t x) {
  return std::in_range<unsigned long int>(x);
}
inline constexpr bool U64FitsSignedLong(uint64_t x) {
  return std::in_range<signed long int>(x);
}

// Don't allow calling these functions with implicit
// conversions.
template<typename T>
void FitsSignedLong(T x) = delete;
template<typename T>
void FitsUnsignedSignedLong(T x) = delete;
template<typename T>
void U64FitsSignedLong(T x) = delete;
}

BigInt::BigInt(std::integral auto ni) {

  using T = decltype(ni);
  if constexpr (std::signed_integral<T>) {
    static_assert(sizeof (T) <= sizeof (int64_t));
    rep = GmpRep(static_cast<int64_t>(ni));
  } else {
    static_assert(std::unsigned_integral<T>);
    static_assert(sizeof (T) <= sizeof (uint64_t));
    const uint64_t u = ni;
    // If it fits in a signed 64-bit integer, we can use
    // the small int representation.
    if (u <= (uint64_t)std::numeric_limits<int64_t>::max()) {
      rep = GmpRep((int64_t)u);
    } else {
      uint64_t u = ni;
      MpzSetU64(rep.Mpz(), u);
    }
  }
}

BigInt::BigInt(const BigInt &other) = default;
BigInt::BigInt(BigInt &&other) noexcept = default;
BigInt &BigInt::operator =(const BigInt &other) = default;
BigInt &BigInt::operator =(BigInt &&other) noexcept = default;

BigInt::BigInt(std::string_view digits) {
  // PERF: Detect small ints.
  rep.Promote();

  // PERF: It would be nice if we could convert without copying,
  // but mpz_set_str wants zero-termination.
  int res = mpz_set_str(rep.Mpz(), std::string(digits).c_str(), 10);
  if (0 != res) {
    printf("Invalid number [%s]\n", std::string(digits).c_str());
    assert(false);
  }
}

BigInt::~BigInt() = default;

void BigInt::Swap(BigInt *other) {
  rep.Swap(&other->rep);
}

uint64_t BigInt::HashCode(const BigInt &a) {
  if (a.rep.IsSmall()) {
    return (uint64_t)a.rep.GetSmall();
  } else {
    // TODO: Include sign?
    // Zero is represented with no limbs.
    size_t limbs = mpz_size(a.rep.ConstMpz());
    if (limbs == 0) return 0;
    // limb 0 is the least significant.
    // XXX if mp_limb_t is not 64 bits, we could get more
    // limbs here.
    return mpz_getlimbn(a.rep.ConstMpz(), 0);
  }
}

std::string BigInt::ToString(int base) const {
  if (rep.IsSmall()) {
    // Worst case would be base 2.
    std::array<char, 65> buffer;
    auto result =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                    rep.GetSmall(), base);

    // Should always succeed.
    assert(result.ec == std::errc());
    return std::string(buffer.data(), result.ptr);

  } else {
    std::string s;
    // We allocate the space directly in the string to avoid
    // copying.
    // May need space for a minus sign. This function also writes
    // a nul terminating byte, but we don't want that for std::string.
    size_t min_size = mpz_sizeinbase(rep.ConstMpz(), base);
    s.resize(min_size + 2);
    mpz_get_str(s.data(), base, rep.ConstMpz());

    // Now we have a nul-terminated string in the buffer, which is at
    // least one byte too large. We could just use strlen here but
    // we know it's at least min_size - 1 (because mpz_sizeinbase
    // can return a number 1 too large). min_size is always at least
    // 1, so starting at min_size - 1 is safe.
    for (size_t sz = min_size - 1; sz < s.size(); sz++) {
      if (s[sz] == 0) {
        s.resize(sz);
        return s;
      }
    }
    // This would mean that mpz_get_str didn't nul-terminate the string.
    assert(false);
    return s;
  }
}

double BigInt::ToDouble() const {
  if (rep.IsSmall()) {
    return (double)rep.GetSmall();
  } else {
    return mpz_get_d(rep.ConstMpz());
  }
}

int BigInt::Sign(const BigInt &a) {
  if (a.rep.IsSmall()) {
    int64_t x = a.rep.GetSmall();
    return (x > 0) - (x < 0);
  } else {
    return mpz_sgn(a.rep.ConstMpz());
  }
}

std::optional<int64_t> BigInt::ToInt() const {
  if (rep.IsSmall()) {
    return rep.GetSmall();
  } else {
    // Get the number of bits, ignoring sign.
    size_t num_bits = mpz_sizeinbase(rep.ConstMpz(), 2);
    if (num_bits <= 63) {
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
                 rep.ConstMpz());

      assert(count <= 1);
      assert(!(digit & 0x8000000000000000ULL));
      if (mpz_sgn(rep.ConstMpz()) == -1) {
        return {-(int64_t)digit};
      }
      return {(int64_t)digit};
    } else if (num_bits == 64) {

      // There is one 64-bit number where we could succeed, which is
      //  std::numeric_limits<int64_t>::lowest().
      if (mpz_sgn(rep.ConstMpz()) == -1 &&
          mpz_getlimbn(rep.ConstMpz(), 0) ==
          uint64_t{0x8000000000000000}) [[unlikely]] {
        // Since we know it's exactly 64 bits, we've uniquely identified
        // the value.
        return std::numeric_limits<int64_t>::lowest();
      }

      return std::nullopt;
    } else {
      return std::nullopt;
    }
  }
}

std::optional<uint64_t> BigInt::ToU64() const {
  if (rep.IsSmall()) {
    int64_t x = rep.GetSmall();
    if (x < 0) return std::nullopt;
    return {(uint64_t)x};
  } else {

    // No negative numbers.
    if (mpz_sgn(rep.ConstMpz()) == -1)
      return std::nullopt;

    // Get the number of bits, ignoring sign.
    if (mpz_sizeinbase(rep.ConstMpz(), 2) > 64) {
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
                 rep.ConstMpz());

      assert(count <= 1);
      return {digit};
    }
  }
}

double BigInt::NaturalLog(const BigInt &a) {
  GmpRep::Lease tmp(a.rep);

  // d is the magnitude, with absolute value in [0.5,1].
  //   a = di * 2^exponent
  // taking the log of both sides,
  //   log(a) = log(di) + log(2) * exponent
  signed long int exponent = 0;
  const double di = mpz_get_d_2exp(&exponent, tmp.ConstMpz());
  return std::log(di) + std::log(2.0) * (double)exponent;
}

double BigInt::LogBase2(const BigInt &a) {
  GmpRep::Lease tmp(a.rep);

  // d is the magnitude, with absolute value in [0.5,1].
  //   a = di * 2^exponent
  // taking the log of both sides,
  //   lg(a) = lg(di) + lg(2) * exponent
  //   lg(a) = log(di)/log(2) + 1 * exponent
  signed long int exponent = 0;
  const double di = mpz_get_d_2exp(&exponent, tmp.ConstMpz());
  return std::log(di)/std::log(2.0) + (double)exponent;
}

size_t BigInt::NumBits(const BigInt &a) {
  if (a.rep.IsSmall()) {
    int64_t aa = a.rep.GetSmall();
    if (aa == 0) return 0;
    if (aa == std::numeric_limits<int64_t>::lowest())
      [[unlikely]] {
      return 64;
    }
    if (aa < 0) aa = -aa;

    return 64 - std::countl_zero<uint64_t>(aa);

  } else {
    GmpRep::Lease a_tmp(a.rep);
    // Otherwise this would return 1.
    if (mpz_sgn(a.rep.ConstMpz()) == 0)
      return 0;

    return mpz_sizeinbase(a_tmp.ConstMpz(), 2);
  }
}

int BigInt::Jacobi(const BigInt &a, const BigInt &b) {
  // PERF: We have an implementation in ../numbers.h.
  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);
  return mpz_jacobi(a_tmp.ConstMpz(), b_tmp.ConstMpz());
}

std::optional<BigInt> BigInt::ModInverse(
    const BigInt &a, const BigInt &b) {
  // PERF: We have an implementation in ../numbers.h.
  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);

  BigInt ret;
  if (mpz_invert(ret.rep.Mpz(), a_tmp.ConstMpz(), b_tmp.ConstMpz())) {
    return {ret};
  } else {
    return std::nullopt;
  }
}


BigInt BigInt::BitwiseAnd(const BigInt &a, const BigInt &b) {
  if (a.rep.IsSmall() && b.rep.IsSmall()) {
    return BigInt(a.rep.GetSmall() & b.rep.GetSmall());
  } else if (a.rep.IsSmall() && !b.rep.IsSmall()) {
    // When one argument is small and non-negative, the result is always small.
    // AND means two's complement, so we can just
    // convert the signed integer to unsigned bits.
    if (a.rep.GetSmall() >= 0) {
      return BigInt(BitwiseAnd(b, std::bit_cast<uint64_t>(a.rep.GetSmall())));
    }
  } else if (!a.rep.IsSmall() && b.rep.IsSmall()) {
    if (b.rep.GetSmall() >= 0) {
      return BigInt(BitwiseAnd(a, std::bit_cast<uint64_t>(b.rep.GetSmall())));
    }
  }

  // Large or negative.
  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);
  BigInt ret;
  mpz_and(ret.rep.Mpz(), a_tmp.ConstMpz(), b_tmp.ConstMpz());
  return ret;
}

uint64_t BigInt::BitwiseAnd(const BigInt &a, uint64_t b) {
  if (a.rep.IsSmall()) {
    return std::bit_cast<uint64_t>(a.rep.GetSmall()) & b;
  } else {
    // Zero is represented without limbs.
    if (mpz_size(a.rep.ConstMpz()) == 0) return 0;
    static_assert(sizeof (mp_limb_t) == 8,
                  "This code assumes 64-bit limbs, although we "
                  "could easily add branches for 32-bit.");
    // Extract the low word and AND natively.
    uint64_t aa = mpz_getlimbn(a.rep.ConstMpz(), 0);
    return aa & b;
  }
}

BigInt BigInt::BitwiseXor(const BigInt &a, const BigInt &b) {
  if (a.rep.IsSmall() && b.rep.IsSmall()) {
    return BigInt(std::bit_cast<uint64_t>(a.rep.GetSmall()) ^
                  std::bit_cast<uint64_t>(b.rep.GetSmall()));
  } else {
    GmpRep::Lease a_tmp(a.rep);
    GmpRep::Lease b_tmp(b.rep);

    BigInt ret;
    mpz_xor(ret.rep.Mpz(), a_tmp.ConstMpz(), b_tmp.ConstMpz());
    return ret;
  }
}

BigInt BigInt::BitwiseOr(const BigInt &a, const BigInt &b) {
  if (a.rep.IsSmall() && b.rep.IsSmall()) {
    return BigInt(std::bit_cast<uint64_t>(a.rep.GetSmall()) |
                  std::bit_cast<uint64_t>(b.rep.GetSmall()));
  } else {
    GmpRep::Lease a_tmp(a.rep);
    GmpRep::Lease b_tmp(b.rep);

    BigInt ret;
    // "inclusive or"
    mpz_ior(ret.rep.Mpz(), a_tmp.ConstMpz(), b_tmp.ConstMpz());
    return ret;
  }
}

uint64_t BigInt::BitwiseCtz(const BigInt &a) {
  if (a.rep.IsSmall()) {
    const int64_t x = a.rep.GetSmall();
    if (x == 0) return 0;
    return std::countr_zero<uint64_t>(
        std::bit_cast<uint64_t>(x));

  } else {
    if (mpz_sgn(a.rep.ConstMpz()) == 0) return 0;
    mp_bitcnt_t zeroes = mpz_scan1(a.rep.ConstMpz(), 0);
    return zeroes;
  }
}

bool BigInt::IsEven() const {
  if (rep.IsSmall()) {
    return (rep.GetSmall() & 0b1) == 0b0;
  } else {
    return mpz_even_p(rep.ConstMpz());
  }
}
bool BigInt::IsOdd() const {
  if (rep.IsSmall()) {
    return (rep.GetSmall() & 0b1) == 0b1;
  } else {
    return mpz_odd_p(rep.ConstMpz());
  }
}

BigInt BigInt::Negate(const BigInt &a) {
  // We need to copy, but then we can just use the rvalue
  // reference version.
  return Negate(BigInt(a));
}
BigInt BigInt::Negate(BigInt &&a) {
  if (a.rep.IsSmall()) {
    int64_t x = a.rep.GetSmall();
    if (x == std::numeric_limits<int64_t>::lowest())
      [[unlikely]] {
      a.rep.Promote();
      mpz_neg(a.rep.Mpz(), a.rep.Mpz());
      return std::move(a);
    } else {
      return BigInt(-x);
    }
  } else {
    mpz_neg(a.rep.Mpz(), a.rep.Mpz());
    return std::move(a);
  }
}

BigInt BigInt::Abs(const BigInt &a) {
  return Abs(BigInt(a));
}
BigInt BigInt::Abs(BigInt &&a) {
  if (a.rep.IsSmall()) {
    int64_t x = a.rep.GetSmall();
    if (x < 0) {
      if (x == std::numeric_limits<int64_t>::lowest())
        [[unlikely]] {
        a.rep.Promote();
        mpz_neg(a.rep.Mpz(), a.rep.Mpz());
        return std::move(a);
      } else {
        return BigInt(-x);
      }
    } else {
      return std::move(a);
    }
  } else {
    mpz_abs(a.rep.Mpz(), a.rep.Mpz());
    return std::move(a);
  }
}


int BigInt::Compare(const BigInt &a, const BigInt &b) {
  if (a.rep.IsSmall() && b.rep.IsSmall()) {
    int64_t aa = a.rep.GetSmall();
    int64_t bb = b.rep.GetSmall();
    if (aa < bb) return -1;
    else if (aa > bb) return 1;
    else return 0;
  } else if (a.rep.IsSmall() && !b.rep.IsSmall()) {
    int64_t aa = a.rep.GetSmall();

    // Yuck: We can't be sure that GMP will be able to
    // fit int64_t in "long int", and we also can't
    // be sure that an integer is actually large just
    // because it has an alloc (it might have just been
    // promoted). So we need to test further cases.
    if (internal::FitsSignedLong(aa)) {
      int r = mpz_cmp_si(b.rep.ConstMpz(), aa);
      // Note: Sense is reversed here.
      if (r < 0) return +1;
      else if (r > 0) return -1;
      else return 0;
    } else {
      GmpRep::Lease aaa(a.rep);
      int r = mpz_cmp(aaa.ConstMpz(), b.rep.ConstMpz());
      if (r < 0) return -1;
      else if (r > 0) return +1;
      else return 0;
    }

  } else if (!a.rep.IsSmall() && b.rep.IsSmall()) {
    // As above.
    int64_t bb = b.rep.GetSmall();

    if (internal::FitsSignedLong(bb)) {
      int r = mpz_cmp_si(a.rep.ConstMpz(), bb);
      if (r < 0) return -1;
      else if (r > 0) return +1;
      else return 0;
    } else {
      GmpRep::Lease bbb(b.rep);
      int r = mpz_cmp(a.rep.ConstMpz(), bbb.ConstMpz());
      if (r < 0) return -1;
      else if (r > 0) return +1;
      else return 0;
    }

  } else {
    // Both have alloc.
    int r = mpz_cmp(a.rep.ConstMpz(), b.rep.ConstMpz());
    if (r < 0) return -1;
    else if (r > 0) return 1;
    else return 0;
  }
}

int BigInt::Compare(const BigInt &a, int64_t b) {
  // As above.

  if (a.rep.IsSmall()) {
    const int64_t aa = a.rep.GetSmall();
    if (aa < b) return -1;
    else if (aa > b) return +1;
    else return 0;

  } else {

    if (internal::FitsSignedLong(b)) {
      int r = mpz_cmp_si(a.rep.ConstMpz(), b);
      if (r < 0) return -1;
      else if (r > 0) return +1;
      else return 0;
    } else {
      GmpRep bbb(b);
      bbb.Promote();
      int r = mpz_cmp(a.rep.ConstMpz(), bbb.ConstMpz());
      if (r < 0) return -1;
      else if (r > 0) return +1;
      else return 0;
    }
  }
}

bool BigInt::Less(const BigInt &a, const BigInt &b) {
  return Compare(a, b) < 0;
}
bool BigInt::Less(const BigInt &a, int64_t b) {
  return Compare(a, b) < 0;
}

bool BigInt::LessEq(const BigInt &a, const BigInt &b) {
  return Compare(a, b) <= 0;
}
bool BigInt::LessEq(const BigInt &a, int64_t b) {
  return Compare(a, b) <= 0;
}

bool BigInt::Eq(const BigInt &a, const BigInt &b) {
  return Compare(a, b) == 0;
}
bool BigInt::Eq(const BigInt &a, int64_t b) {
  return Compare(a, b) == 0;
}


bool BigInt::Greater(const BigInt &a, const BigInt &b) {
  return Compare(a, b) > 0;
}
bool BigInt::Greater(const BigInt &a, int64_t b) {
  return Compare(a, b) > 0;
}


bool BigInt::GreaterEq(const BigInt &a, const BigInt &b) {
  return Compare(a, b) >= 0;
}
bool BigInt::GreaterEq(const BigInt &a, int64_t b) {
  return Compare(a, b) >= 0;
}

void BigInt::PlusEq(BigInt &a, const BigInt &b) {
  if (a.rep.IsSmall() && b.rep.IsSmall()) {
    int64_t res;
    // We could have replacements for functions like this when
    // not using clang/gcc, but there's also the simple fallback
    // of not using the GMP codepath.
    if (!__builtin_add_overflow(a.rep.GetSmall(), b.rep.GetSmall(), &res)) {
      a.rep.UpdateSmall(res);
      return;
    }
    // Note fallthrough to general case on overflow.

  } else if (b.rep.IsSmall()) {
    int64_t bb = b.rep.GetSmall();
    if (bb >= 0 && internal::FitsUnsignedLong(bb)) {
      unsigned long int sb = bb;
      mpz_add_ui(a.rep.Mpz(), a.rep.Mpz(), sb);
      return;
    }
    // Fall through if it doesn't fit in signed long.

  }

  GmpRep::Lease b_tmp(b.rep);
  mpz_add(a.rep.Mpz(), a.rep.Mpz(), b_tmp.ConstMpz());
}

BigInt BigInt::Plus(const BigInt &a, const BigInt &b) {
  if (a.rep.IsSmall() && b.rep.IsSmall()) {
    int64_t res;
    // We could have replacements for functions like this when
    // not using clang/gcc, but there's also the simple fallback
    // of not using the GMP codepath.
    if (!__builtin_add_overflow(a.rep.GetSmall(), b.rep.GetSmall(), &res)) {
      return BigInt(res);
    }
    // Note fallthrough to general case on overflow.

  } else if (b.rep.IsSmall()) {
    int64_t bb = b.rep.GetSmall();
    if (bb >= 0 && internal::FitsUnsignedLong(bb)) {
      unsigned long int sb = bb;
      BigInt ret;
      GmpRep::Lease a_tmp(a.rep);
      mpz_add_ui(ret.rep.Mpz(), a_tmp.ConstMpz(), sb);
      return ret;
    }
    // Fall through if it doesn't fit in signed long.

  } else if (a.rep.IsSmall()) {
    int64_t aa = a.rep.GetSmall();
    if (aa >= 0 && internal::FitsUnsignedLong(aa)) {
      unsigned long int sa = aa;
      BigInt ret;
      GmpRep::Lease b_tmp(b.rep);
      mpz_add_ui(ret.rep.Mpz(), b_tmp.ConstMpz(), sa);
      return ret;
    }
    // Fall through if it doesn't fit in signed long.

  }

  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);

  BigInt ret;
  mpz_add(ret.rep.Mpz(), a_tmp.ConstMpz(), b_tmp.ConstMpz());
  return ret;
}


BigInt BigInt::Minus(const BigInt &a, const BigInt &b) {

  if (a.rep.IsSmall() && b.rep.IsSmall()) {
    int64_t res;
    if (!__builtin_sub_overflow(a.rep.GetSmall(), b.rep.GetSmall(), &res)) {
      return BigInt(res);
    }

    // Note fallthrough to general case on overflow.
  }

  // TODO PERF: Detect sub_si cases.

  /*
      // PERF could also support negative b. but GMP only has
  // _ui version.
  if (b >= 0 && internal::FitsLongInt(b)) {
    signed long int sb = b;
    BigInt ret;
    mpz_sub_ui(ret.rep, a.rep, sb);
    return ret;
  } else {
    return Minus(a, BigInt(b));
  }
  */

  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);

  BigInt ret;
  mpz_sub(ret.rep.Mpz(), a_tmp.ConstMpz(), b_tmp.ConstMpz());
  return ret;
}

BigInt BigInt::Times(const BigInt &a, const BigInt &b) {

  if (a.rep.IsSmall() && b.rep.IsSmall()) {
    int64_t res;
    if (!__builtin_mul_overflow(a.rep.GetSmall(), b.rep.GetSmall(), &res)) {
      return BigInt(res);
    }

    // Note fallthrough to general case on overflow.
  }

  // TODO PERF: Detect mul_si cases.

  /*
  // PERF could also support negative b. but GMP only has
  // _ui version.
  if (b >= 0 && internal::FitsLongInt(b)) {
    signed long int sb = b;
    BigInt ret;
    mpz_sub_ui(ret.rep, a.rep, sb);
    return ret;
  } else {
    return Minus(a, BigInt(b));
  }
  */

  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);

  BigInt ret;
  mpz_mul(ret.rep.Mpz(), a_tmp.ConstMpz(), b_tmp.ConstMpz());
  return ret;
}

BigInt BigInt::Div(const BigInt &a, const BigInt &b) {
  // PERF: Handle small case, integer versions.

  /*
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
  */

  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);

  // truncate (round towards zero) like C
  BigInt ret;
  mpz_tdiv_q(ret.rep.Mpz(), a_tmp.ConstMpz(), b_tmp.ConstMpz());
  return ret;
}

BigInt BigInt::Div(const BigInt &a, int64_t b) {
  return Div(a, BigInt(b));
}

BigInt BigInt::DivFloor(const BigInt &a, const BigInt &b) {
  // truncate (round towards zero) like C

  // PERF: Handle small case, integer versions.
  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);

  BigInt ret;
  mpz_fdiv_q(ret.rep.Mpz(), a_tmp.ConstMpz(), b_tmp.ConstMpz());
  return ret;
}

BigInt BigInt::DivFloor(const BigInt &a, int64_t b) {
  // PERF: There is mpz_fdiv_q_ui, but we can't just flip the
  // sign if it's negative, since rounding depends on the sign.
  // Maybe just handle the positive case here?
  return DivFloor(a, BigInt(b));
}


bool BigInt::DivisibleBy(const BigInt &num, const BigInt &den) {
  GmpRep::Lease num_tmp(num.rep);
  GmpRep::Lease den_tmp(den.rep);

  /* PERF
      if (internal::FitsLongInt(den)) {
    unsigned long int uden = std::abs(den);
    return mpz_divisible_ui_p(num.rep, uden);
  } else {
    return DivisibleBy(num, BigInt(den));
  }
  */

  // (Note that GMP accepts 0 % 0, but I consider that an instance
  // of undefined behavior in this library.)
  return mpz_divisible_p(num_tmp.ConstMpz(), den_tmp.ConstMpz());
}

bool BigInt::DivisibleBy(const BigInt &num, int64_t den) {
  return DivisibleBy(num, BigInt(den));
}

BigInt BigInt::DivExact(const BigInt &num, const BigInt &den) {
  GmpRep::Lease num_tmp(num.rep);
  GmpRep::Lease den_tmp(den.rep);

  /*
      if (internal::FitsLongInt(b)) {
    if (b >= 0) {
      BigInt ret;
      unsigned long int ub = b;
      mpz_divexact_ui(ret.rep, a.rep, ub);
      return ret;
    } else {
      unsigned long int ub = -b;
      BigInt ret;
      mpz_divexact_ui(ret.rep, a.rep, ub);
      mpz_neg(ret.rep, ret.rep);
      return ret;
    }
  } else {
    return DivExact(a, BigInt(b));
  }
  */

  BigInt ret;
  mpz_divexact(ret.rep.Mpz(), num_tmp.ConstMpz(), den_tmp.ConstMpz());
  return ret;
}

BigInt BigInt::DivExact(const BigInt &a, int64_t b) {
  return DivExact(a, BigInt(b));
}

BigInt BigInt::Mod(const BigInt &a, const BigInt &b) {
  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);

  BigInt ret;
  mpz_mod(ret.rep.Mpz(), a_tmp.ConstMpz(), b_tmp.ConstMpz());
  return ret;
}

BigInt BigInt::CMod(const BigInt &a, const BigInt &b) {
  if (a.rep.IsSmall() && b.rep.IsSmall()) {
    // Can use %, but need to check for b == limits::lowest
  }

  /*
    PERF: Should still use _ui version here when possible
   */

  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);

  BigInt r;
  mpz_tdiv_r(r.rep.Mpz(), a_tmp.ConstMpz(), b_tmp.ConstMpz());
  return r;
}

int64_t BigInt::CMod(const BigInt &a, int64_t b) {
  if (internal::FitsSignedLong(b)) {
    if (b >= 0) {
      // PERF: Could check for a small.
      GmpRep::Lease a_tmp(a.rep);
      unsigned long int ub = b;
      // PERF: Should be possible to do this without
      // allocating a rep? The return value is the
      // absolute value of the remainder.
      BigInt ret;
      (void)mpz_tdiv_r_ui(ret.rep.Mpz(), a_tmp.ConstMpz(), ub);
      auto ro = ret.ToInt();
      assert(ro.has_value());
      return ro.value();
    } else {
      // TODO: Can still use tdiv_r_ui.
    }
  }

  auto ro = CMod(a, BigInt(b)).ToInt();
  assert(ro.has_value());
  return ro.value();
}

// Returns Q (a div b), R (a mod b) such that a = b * q + r
std::pair<BigInt, BigInt> BigInt::QuotRem(const BigInt &a,
                                          const BigInt &b) {
  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);

  BigInt q, r;
  mpz_tdiv_qr(q.rep.Mpz(), r.rep.Mpz(),
              a_tmp.ConstMpz(), b_tmp.ConstMpz());
  return std::make_pair(std::move(q), std::move(r));
}

BigInt BigInt::Pow(const BigInt &a, uint64_t exponent) {
  GmpRep::Lease a_tmp(a.rep);

  BigInt ret;
  mpz_pow_ui(ret.rep.Mpz(), a_tmp.ConstMpz(), exponent);
  return ret;
}

BigInt BigInt::LeftShift(const BigInt &a, uint64_t shift) {
  // PERF: Easy when small

  if (internal::U64FitsSignedLong(shift)) [[likely]] {
    GmpRep::Lease a_tmp(a.rep);
    mp_bitcnt_t sh = shift;
    BigInt ret;
    mpz_mul_2exp(ret.rep.Mpz(), a_tmp.ConstMpz(), sh);
    return ret;
  } else {
    // If the shift amount is too big to fit in long int, then this
    // will probably only be possible if is -1, 0, or 1. But we
    // can at least do as the programmer asked...
    return Times(a, Pow(BigInt{2}, shift));
  }
}

BigInt BigInt::RightShift(const BigInt &a, uint64_t shift) {
  // PERF: Even easier when small, since it can't overflow
  if (internal::U64FitsSignedLong(shift)) {
    GmpRep::Lease a_tmp(a.rep);
    mp_bitcnt_t sh = shift;
    BigInt ret;
    mpz_fdiv_q_2exp(ret.rep.Mpz(), a_tmp.ConstMpz(), sh);
    return ret;
  } else {
    return Div(a, Pow(BigInt{2}, shift));
  }
}


BigInt BigInt::GCD(const BigInt &a, const BigInt &b) {
  // PERF: We have this for 64 bits in ../numbers.h.
  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);

  BigInt ret;
  mpz_gcd(ret.rep.Mpz(), a_tmp.ConstMpz(), b_tmp.ConstMpz());
  return ret;
}

std::tuple<BigInt, BigInt, BigInt>
BigInt::ExtendedGCD(const BigInt &a, const BigInt &b) {
  // PERF: We have this for 64 bits in ../numbers.h.
  GmpRep::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);

  BigInt g, s, t;
  mpz_gcdext(g.rep.Mpz(), s.rep.Mpz(), t.rep.Mpz(),
             a_tmp.ConstMpz(), b_tmp.ConstMpz());
  return std::make_tuple(std::move(g), std::move(s), std::move(t));
}

BigInt BigInt::Sqrt(const BigInt &aa) {
  if (aa.rep.IsSmall()) {
    int64_t n = aa.rep.GetSmall();
    if (n < 0) [[unlikely]] {
      abort();
    }

    if (n == 0) return BigInt(0);

    // Not obvious that using double precision floating point is
    // correct for all 64-bit integers, but it does work. At most
    // we will be off by 1, so we subtract that off.
    int64_t r = std::sqrt((double)n);
    int64_t overage = r * r - 1 >= n;
    return BigInt(r - overage);

  } else {
    BigInt ret;
    mpz_sqrt(ret.rep.Mpz(), aa.rep.ConstMpz());
    return ret;
  }
}

std::pair<BigInt, BigInt> BigInt::SqrtRem(const BigInt &aa) {
  GmpRep::Lease aa_tmp(aa.rep);

  BigInt ret, rem;
  mpz_sqrtrem(ret.rep.Mpz(), rem.rep.Mpz(), aa_tmp.ConstMpz());
  return std::make_pair(std::move(ret), std::move(rem));
}

// GMP Rationals.

BigRat::BigRat(int64_t numer, int64_t denom) :
  BigRat(BigInt{numer}, BigInt{denom}) {}

BigRat::BigRat(int64_t numer) : BigRat(BigInt{numer}) {}

BigRat::BigRat(const BigRat &other) = default;
BigRat::BigRat(BigRat &&other) noexcept = default;

BigRat &BigRat::operator =(const BigRat &other) = default;
BigRat &BigRat::operator =(BigRat &&other) noexcept = default;

BigRat::BigRat(const BigInt &numer, const BigInt &denom) {
  if (numer.rep.IsSmall() && denom.rep.IsSmall()) {
    // This canonicalizes.
    rep = GmpRepRat(numer.rep.GetSmall(), denom.rep.GetSmall());

  } else {
    // PERF: Promoting initializes with zeroes only to overwrite.
    rep.Promote();

    GmpRep::Lease numer_tmp(numer.rep);
    GmpRep::Lease denom_tmp(denom.rep);

    mpz_set(mpq_numref(rep.Mpq()), numer_tmp.ConstMpz());
    mpz_set(mpq_denref(rep.Mpq()), denom_tmp.ConstMpz());
    mpq_canonicalize(rep.Mpq());
  }
}

BigRat::BigRat(const BigInt &numer) {
  if (numer.rep.IsSmall()) {
    // This canonicalizes.
    rep = GmpRepRat(numer.rep.GetSmall());

  } else {
    // PERF: Promoting initializes with zeroes only to overwrite.
    rep.Promote();

    GmpRep::Lease numer_tmp(numer.rep);
    mpq_set_z(rep.Mpq(), numer_tmp.ConstMpz());
  }
}

void BigRat::Swap(BigRat *other) {
  rep.Swap(&other->rep);
}

BigRat::~BigRat() = default;

int BigRat::Compare(const BigRat &a, const BigRat &b) {
  // PERF: Small case is definitely important here, and then
  // we can remove the int64 overloads!

  GmpRepRat::Lease a_tmp(a.rep);
  GmpRepRat::Lease b_tmp(b.rep);

  const int r = mpq_cmp(a_tmp.ConstMpq(), b_tmp.ConstMpq());
  if (r < 0) return -1;
  else if (r > 0) return 1;
  else return 0;
}

int BigRat::Compare(const BigRat &a, const BigInt &b) {
  // PERF: Small case.

  GmpRepRat::Lease a_tmp(a.rep);
  GmpRep::Lease b_tmp(b.rep);

  const int r = mpq_cmp_z(a_tmp.ConstMpq(), b_tmp.ConstMpz());
  if (r < 0) return -1;
  else if (r > 0) return 1;
  else return 0;
}

bool BigRat::Eq(const BigRat &a, const BigRat &b) {
  if (a.rep.IsSmall() && b.rep.IsSmall()) {
    int64_t an = a.rep.SmallNumer();
    int64_t ad = a.rep.SmallDenom();

    int64_t bn = b.rep.SmallNumer();
    int64_t bd = b.rep.SmallDenom();

    if (an != bn) return false;

    // If numerator is zero, any denominator is allowed.
    if (an == 0) return true;
    return ad == bd;
  } else {
    GmpRepRat::Lease a_tmp(a.rep);
    GmpRepRat::Lease b_tmp(b.rep);
    const int r = mpq_equal(a_tmp.ConstMpq(), b_tmp.ConstMpq());
    return r != 0;
  }
}

bool BigRat::Eq(const BigRat &a, int64_t b) {
  if (b == 0) {
    return BigRat::Sign(a) == 0;
  }

  if (a.rep.IsSmall()) {
    return a.rep.SmallDenom() == 1 && a.rep.SmallNumer() == b;
  } else {
    // To be equal to an integer, the denominator must be 1.
    if (mpz_cmp_si(mpq_denref(a.rep.ConstMpq()), 1) != 0)
      return false;

    if (internal::FitsSignedLong(b)) {
      signed long int sb = b;
      return mpz_cmp_si(mpq_numref(a.rep.ConstMpq()), sb) == 0;
    } else {
      BigInt rhs(b);
      rhs.rep.Promote();
      return mpz_cmp(mpq_numref(a.rep.ConstMpq()),
                     rhs.rep.ConstMpz()) == 0;
    }
  }
}

BigRat BigRat::Abs(const BigRat &a) {
  BigRat ret;
  GmpRepRat::Lease a_tmp(a.rep);
  mpq_abs(ret.rep.Mpq(), a_tmp.ConstMpq());
  return ret;
}

BigRat BigRat::Div(const BigRat &a, const BigRat &b) {
  BigRat ret;
  GmpRepRat::Lease a_tmp(a.rep);
  GmpRepRat::Lease b_tmp(b.rep);
  mpq_div(ret.rep.Mpq(), a_tmp.ConstMpq(), b_tmp.ConstMpq());
  return ret;
}

BigRat BigRat::Inverse(const BigRat &a) {
  BigRat ret;
  GmpRepRat::Lease a_tmp(a.rep);
  mpq_inv(ret.rep.Mpq(), a_tmp.ConstMpq());
  return ret;
}
BigRat BigRat::Times(const BigRat &a, const BigRat &b) {
  BigRat ret;
  GmpRepRat::Lease a_tmp(a.rep);
  GmpRepRat::Lease b_tmp(b.rep);
  mpq_mul(ret.rep.Mpq(), a_tmp.ConstMpq(), b_tmp.ConstMpq());
  return ret;
}

BigRat BigRat::Negate(const BigRat &a) {
  BigRat ret;
  GmpRepRat::Lease a_tmp(a.rep);
  mpq_neg(ret.rep.Mpq(), a_tmp.ConstMpq());
  return ret;
}
BigRat BigRat::Negate(BigRat &&a) {
  mpq_neg(a.rep.Mpq(), a.rep.Mpq());
  return std::move(a);
}

BigRat BigRat::Plus(const BigRat &a, const BigRat &b) {
  BigRat ret;
  GmpRepRat::Lease a_tmp(a.rep);
  GmpRepRat::Lease b_tmp(b.rep);
  mpq_add(ret.rep.Mpq(), a_tmp.ConstMpq(), b_tmp.ConstMpq());
  return ret;
}

BigRat BigRat::Minus(const BigRat &a, const BigRat &b) {
  BigRat ret;
  GmpRepRat::Lease a_tmp(a.rep);
  GmpRepRat::Lease b_tmp(b.rep);
  mpq_sub(ret.rep.Mpq(), a_tmp.ConstMpq(), b_tmp.ConstMpq());
  return ret;
}

std::string BigRat::ToString() const {
  const auto &[numer, denom] = Parts();
  std::string ns = numer.ToString();
  if (BigInt::Eq(denom, BigInt(1))) {
    // for n/1
    return ns;
  } else {
    std::string ds = denom.ToString();
    return std::move(ns) + "/" + ds;
  }
}

std::pair<BigInt, BigInt> BigRat::Parts() const {
  if (rep.IsSmall()) {
    return std::make_pair(BigInt(rep.SmallNumer()),
                          BigInt(rep.SmallDenom()));
  } else {
    BigInt numer;
    mpz_set(numer.rep.Mpz(), mpq_numref(rep.ConstMpq()));
    if (BigInt::Sign(numer) == 0) {
      // Ensure we don't get 0/d for d!=1.
      return std::make_pair(std::move(numer), BigInt(1));
    } else {
      BigInt denom;
      mpz_set(denom.rep.Mpz(), mpq_denref(rep.ConstMpq()));
      return std::make_pair(std::move(numer), std::move(denom));
    }
  }
}

BigInt BigRat::Numerator() const {
  if (rep.IsSmall()) {
    return BigInt(rep.SmallNumer());
  } else {
    BigInt numer;
    mpz_set(numer.rep.Mpz(), mpq_numref(rep.ConstMpq()));
    return numer;
  }
}

BigInt BigRat::Denominator() const {
  if (rep.IsSmall()) {
    if (rep.SmallNumer() == 0) {
      // Ensure we return a denominator of 1 for 0/d.
      return BigInt(1);
    }

    return BigInt(rep.SmallDenom());
  } else {
    if (mpz_sgn(mpq_numref(rep.ConstMpq())) == 0) {
      // Ensure we return a denominator of 1 for 0/d.
      return BigInt(1);
    } else {
      BigInt denom;
      mpz_set(denom.rep.Mpz(), mpq_denref(rep.ConstMpq()));
      return denom;
    }
  }
}

BigRat BigRat::FromDouble(double num) {
  // PERF: Could detect small integers pretty easily.
  BigRat ret;
  mpq_set_d(ret.rep.Mpq(), num);
  return ret;
}

BigRat BigRat::ApproxDouble(double num, int64_t max_denom) {
  // XXX implement max_denom somehow?
  BigRat ret;
  mpq_set_d(ret.rep.Mpq(), num);
  return ret;
}

double BigRat::ToDouble() const {
  if (rep.IsSmall()) {
    return rep.SmallNumer() / (double)rep.SmallDenom();
  } else {
    return mpq_get_d(rep.ConstMpq());
  }
}

uint64_t BigRat::HashCode(const BigRat &a) {
  // PERF: Probably should avoid copying when hashing small
  // numbers. But we need the hash code to be the same for a
  // given number whether the representation is small or large.
  // Might be good to do this as a mathematical function, then?
  GmpRepRat::Lease a_tmp(a.rep);

  const auto &nrep = mpq_numref(a_tmp.ConstMpq());
  const size_t nlimbs = mpz_size(nrep);
  const auto &drep = mpq_denref(a_tmp.ConstMpq());
  const size_t dlimbs = mpz_size(drep);

  uint64_t h = 0xC0FFEE'777'1234567;
  if (dlimbs != 0) {
    h ^= mpz_getlimbn(drep, 0);
    h = std::rotl<uint64_t>(h, 51);
    h *= uint64_t{6364136223846793005};
  }

  if (nlimbs != 0) {
    if (mpz_sgn(nrep) == -1) {
      h ^= h >> 18;
    }

    h += mpz_getlimbn(nrep, 0);
  }

  return h;
}

int BigRat::Sign(const BigRat &a) {
  if (a.rep.IsSmall()) {
    int64_t x = a.rep.SmallNumer();
    return (x > 0) - (x < 0);
  } else {
    return mpz_sgn(mpq_numref(a.rep.ConstMpq()));
  }
}

#else
// No GMP. Using portable big*.h.


BigInt::BigInt(std::integral auto ni) {
  // PERF: Set 32-bit quantities too.
  /*
  // Need to be able to set 4 bytes at a time.
  static_assert(sizeof (unsigned long int) >= 4);
  mpz_set_ui(rep, u);
  */

  using T = decltype(ni);
  if constexpr (std::signed_integral<T>) {
    static_assert(sizeof (T) <= sizeof (int64_t));
    const int64_t n = ni;
    rep = BzFromInteger(n);
  } else {
    static_assert(std::unsigned_integral<T>);
    static_assert(sizeof (T) <= sizeof (uint64_t));
    const uint64_t u = ni;
    rep = BzFromUnsignedInteger(u);
  }
}

BigInt::BigInt(const BigInt &other) : rep(BzCopy(other.rep)) { }
BigInt::BigInt(BigInt &&other) noexcept : rep(other.rep) {
  // Take ownership. Only valid thing to do with other is
  // destroy it.
  other.rep = nullptr;
}

BigInt &BigInt::operator =(const BigInt &other) {
  // Self-assignment does nothing.
  if (this == &other) return *this;
  BzFree(rep);
  rep = BzCopy(other.rep);
  return *this;
}
BigInt &BigInt::operator =(BigInt &&other) noexcept {
  // We don't care how we leave other, but it needs to be valid (e.g. for
  // the destructor). Swap is a good way to do this.
  Swap(&other);
  return *this;
}

BigInt::~BigInt() {
  // Note: rep can be null (source of move constructor), but
  // BzFree can free null (it's just free()).
  BzFree(rep);
  rep = nullptr;
}

void BigInt::Swap(BigInt *other) {
  std::swap(rep, other->rep);
}

BigInt::BigInt(std::string_view digits) {
  rep = BzFromStringLen(digits.data(), digits.size(), 10, BZ_UNTIL_END);
}

std::string BigInt::ToString(int base) const {
  // Allocates a buffer.
  // Third argument forces a + sign for positive; not used here.
  BzChar *buf = BzToString(rep, base, 0);
  std::string ret{buf};
  BzFreeString(buf);
  return ret;
}

int BigInt::Sign(const BigInt &a) {
  switch (BzGetSign(a.rep)) {
  case BZ_MINUS: return -1;
  default:
  case BZ_ZERO: return 0;
  case BZ_PLUS: return 1;
  }
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
std::optional<uint64_t> BigInt::ToU64() const {
  if (BzGetSign(rep) == BZ_MINUS ||
      BzNumDigits(rep) > (BigNumLength)1) {
    return std::nullopt;
  } else {
    const uint64_t digit = BzGetDigit(rep, 0);
    // Would overflow int64. (This may be a bug in BzToInteger?)
    return {(uint64_t)digit};
  }
}

double BigInt::ToDouble() const {
  std::optional<int64_t> io = ToInt();
  if (io.has_value()) return (double)io.value();
  // There is no doubt a more accurate and faster way to do this!

  const int size = BzGetSize(rep);

  double d = 0.0;
  // From big end to little
  for (int idx = size - 1; idx >= 0; idx--) {
    uint64_t udigit = BzGetDigit(rep, idx);
    double ddigit = udigit;
    // printf("d %.17g | u %llu | dd %.17g\n", d, udigit, ddigit);
    for (size_t e = 0; e < sizeof (BigNumDigit); e++)
      d *= 256.0;
    d += ddigit;
  }

  if (BzGetSign(rep) == BZ_MINUS) d = -d;
  return d;
}

uint64_t BigInt::HashCode(const BigInt &a) {
  // TODO: Include sign?
  if (BzNumDigits(a.rep) == 0) return uint64_t{0};
  else return BzGetDigit(a.rep, 0);
}

bool BigInt::IsEven() const { return BzIsEven(rep); }
bool BigInt::IsOdd() const { return BzIsOdd(rep); }

int BigInt::Jacobi(const BigInt &a_input,
                   const BigInt &n_input) {
  // FIXME: Buggy! Test segfaults.
  // (I think I did fix it and this comment is stale, but
  // I should verify!)

  // Preconditions.
  assert(Greater(n_input, 0));
  assert(n_input.IsOdd());

  int t = 1;

  BigInt a = CMod(a_input, n_input);
  BigInt n = n_input;

  if (Less(a, 0)) a = Plus(a, n);

  while (!Eq(a, 0)) {

    while (a.IsEven()) {
      a = RightShift(a, 1);
      const uint64_t r = BitwiseAnd(n, 7);
      if (r == 3 || r == 5) {
        t = -t;
      }
    }

    std::swap(n, a);
    if (BitwiseAnd(a, 3) == 3 &&
        BitwiseAnd(n, 3) == 3) {
      t = -t;
    }

    a = CMod(a, n);
  }

  if (Eq(n, 1)) {
    return t;
  } else {
    return 0;
  }
}

size_t BigInt::NumBits(const BigInt &a) {
  int s = Sign(a);
  if (s == 0) return 0;

  BigInt x = a;

  if (s == -1) {
    x = BigInt::Negate(std::move(x));
  }

  // PERF: Can go in larger chunks
  size_t bits = 0;
  while (BigInt::Sign(x) != 0) {
    bits++;
    x = BigInt::RightShift(x, 1);
  }

  return bits;
}

std::tuple<BigInt, BigInt, BigInt>
BigInt::ExtendedGCD(const BigInt &a, const BigInt &b) {
  BigInt s{0}, old_s{1};
  BigInt t{1}, old_t{0};
  BigInt r = b, old_r = a;

  while (!BigInt::IsZero(r)) {
    auto [quotient, rem] = BigInt::QuotRem(old_r, r);

    old_r = std::move(r);
    r = std::move(rem);

    // Update Bezout coefficients s and t.
    BigInt next_s = BigInt::Minus(old_s, BigInt::Times(quotient, s));
    old_s = std::move(s);
    s = std::move(next_s);

    BigInt next_t = BigInt::Minus(old_t, BigInt::Times(quotient, t));
    old_t = std::move(t);
    t = std::move(next_t);
  }

  // Now we have
  // g = old_r
  // s = old_s
  // t = old_t

  // The GCD is conventionally non-negative. If old_r is negative
  // (which can happen if inputs are negative), we negate all three
  // results to maintain the identity a*s + b*t = g.
  if (BigInt::Sign(old_r) < 0) {
    old_r = BigInt::Negate(std::move(old_r));
    old_s = BigInt::Negate(std::move(old_s));
    old_t = BigInt::Negate(std::move(old_t));
  }

  return std::make_tuple(std::move(old_r),
                         std::move(old_s),
                         std::move(old_t));
}

std::optional<BigInt> BigInt::ModInverse(
    const BigInt &a, const BigInt &b) {
  // The modulus b must be positive.
  if (BigInt::LessEq(b, 0)) {
    return std::nullopt;
  }

  //   a*s + b*t = g
  // GCD g must be 1. So then we have
  //   a*s + 0*t = 1 (mod b)  =>  a*s = 1 (mod b)
  // So s is the inverse we're looking for.
  auto [g, s, t] = BigInt::ExtendedGCD(a, b);

  if (!BigInt::Eq(g, 1)) {
    // No inverse exists.
    return std::nullopt;
  }

  // Ensure s is in the canonical range [0, b - 1].
  return std::make_optional(Mod(s, b));
}

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
BigInt BigInt::Abs(BigInt &&a) {
  // PERF abs in place?
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
bool BigInt::Less(const BigInt &a, int64_t b) {
  return BzCompare(a.rep, BigInt{b}.rep) == BZ_LT;
}

bool BigInt::LessEq(const BigInt &a, const BigInt &b) {
  auto cmp = BzCompare(a.rep, b.rep);
  return cmp == BZ_LT || cmp == BZ_EQ;
}
bool BigInt::LessEq(const BigInt &a, int64_t b) {
  auto cmp = BzCompare(a.rep, BigInt{b}.rep);
  return cmp == BZ_LT || cmp == BZ_EQ;
}

bool BigInt::Eq(const BigInt &a, const BigInt &b) {
  return BzCompare(a.rep, b.rep) == BZ_EQ;
}
bool BigInt::Eq(const BigInt &a, int64_t b) {
  return BzCompare(a.rep, BigInt{b}.rep) == BZ_EQ;
}

bool BigInt::Greater(const BigInt &a, const BigInt &b) {
  return BzCompare(a.rep, b.rep) == BZ_GT;
}
bool BigInt::Greater(const BigInt &a, int64_t b) {
  return BzCompare(a.rep, BigInt{b}.rep) == BZ_GT;
}

bool BigInt::GreaterEq(const BigInt &a, const BigInt &b) {
  auto cmp = BzCompare(a.rep, b.rep);
  return cmp == BZ_GT || cmp == BZ_EQ;
}
bool BigInt::GreaterEq(const BigInt &a, int64_t b) {
  auto cmp = BzCompare(a.rep, BigInt{b}.rep);
  return cmp == BZ_GT || cmp == BZ_EQ;
}

BigInt BigInt::Plus(const BigInt &a, const BigInt &b) {
  return BigInt{BzAdd(a.rep, b.rep), nullptr};
}
void BigInt::PlusEq(BigInt &a, const BigInt &b) {
  a = BigInt{BzAdd(a.rep, b.rep), nullptr};
}

BigInt BigInt::Minus(const BigInt &a, const BigInt &b) {
  return BigInt{BzSubtract(a.rep, b.rep), nullptr};
}

BigInt BigInt::Times(const BigInt &a, const BigInt &b) {
  return BigInt{BzMultiply(a.rep, b.rep), nullptr};
}

BigInt BigInt::Div(const BigInt &a, const BigInt &b) {
  // In BzTruncate is truncating division, like C.
  return BigInt{BzTruncate(a.rep, b.rep), nullptr};
}
BigInt BigInt::Div(const BigInt &a, int64_t b) {
  return BigInt{BzTruncate(a.rep, BigInt{b}.rep), nullptr};
}

BigInt BigInt::DivFloor(const BigInt &a, const BigInt &b) {
  return BigInt{BzFloor(a.rep, b.rep), nullptr};
}
BigInt BigInt::DivFloor(const BigInt &a, int64_t b) {
  return BigInt{BzFloor(a.rep, BigInt{b}.rep), nullptr};
}


BigInt BigInt::DivExact(const BigInt &a, const BigInt &b) {
  // Not using the precondition here; same as division.
  return BigInt{BzTruncate(a.rep, b.rep), nullptr};
}
BigInt BigInt::DivExact(const BigInt &a, int64_t b) {
  // Not using the precondition here; same as division.
  return BigInt{BzTruncate(a.rep, BigInt{b}.rep), nullptr};
}

bool BigInt::DivisibleBy(const BigInt &num, const BigInt &den) {
  return BzCompare(CMod(num, den).rep, BigInt{0}.rep) == BZ_EQ;
}
bool BigInt::DivisibleBy(const BigInt &num, int64_t den) {
  return BzCompare(CMod(num, BigInt{den}).rep, BigInt{0}.rep) == BZ_EQ;
}

// TODO: Clarify mod vs rem?
BigInt BigInt::Mod(const BigInt &a, const BigInt &b) {
  return BigInt{BzMod(a.rep, b.rep), nullptr};
}

// Returns Q (a div b), R (a mod b) such that a = b * q + r
std::pair<BigInt, BigInt> BigInt::QuotRem(const BigInt &a,
                                          const BigInt &b) {
  BigZ r = BzRem(a.rep, b.rep);
  BigZ q = BzTruncate(a.rep, b.rep);
  return std::make_pair(BigInt{q, nullptr}, BigInt{r, nullptr});
}

BigInt BigInt::CMod(const BigInt &a, const BigInt &b) {
  return BigInt{BzRem(a.rep, b.rep), nullptr};
}

int64_t BigInt::CMod(const BigInt &a, int64_t b) {
  auto ro = BigInt{BzRem(a.rep, BigInt{b}.rep), nullptr}.ToInt();
  assert(ro.has_value());
  return ro.value();
}


BigInt BigInt::Pow(const BigInt &a, uint64_t exponent) {
  return BigInt{BzPow(a.rep, exponent), nullptr};
}

BigInt BigInt::LeftShift(const BigInt &a, uint64_t bits) {
  return Times(a, Pow(BigInt{2}, bits));
}

BigInt BigInt::RightShift(const BigInt &a, uint64_t bits) {
  // There is BzAsh which I assume is Arithmetic Shift?
  return BigInt{BzFloor(a.rep, Pow(BigInt{2}, bits).rep), nullptr};
}

BigInt BigInt::BitwiseAnd(const BigInt &a, const BigInt &b) {
  return BigInt{BzAnd(a.rep, b.rep), nullptr};
}

uint64_t BigInt::BitwiseAnd(const BigInt &a, uint64_t b) {
  // PERF: extract a as int (just need low word), then do native and.
  // return BigInt{BzAnd(a.rep, BigInt{b}.rep), nullptr};
  auto ao = BigInt{BzAnd(a.rep, BigInt{b}.rep), nullptr}.ToU64();
  // Result must fit in 64 bits, since we anded with a 64-bit number.
  assert(ao.has_value());
  return ao.value();
}

BigInt BigInt::BitwiseXor(const BigInt &a, const BigInt &b) {
  return BigInt{BzXor(a.rep, b.rep), nullptr};
}

BigInt BigInt::BitwiseOr(const BigInt &a, const BigInt &b) {
  return BigInt{BzOr(a.rep, b.rep), nullptr};
}


uint64_t BigInt::BitwiseCtz(const BigInt &a) {
  if (BzGetSign(a.rep) == BZ_ZERO) return 0;
  // PERF: This could be faster by testing a limb at a time first.
  uint64_t bit = 0;
  while (!BzTestBit(bit, a.rep)) bit++;
  return bit;
}

double BigInt::LogBase2(const BigInt &a) {
  return LogBase2Internal(a);
}

double BigInt::NaturalLog(const BigInt &a) {
  return LogBase2Internal(a) * std::log(2.0);
}

BigInt BigInt::GCD(const BigInt &a, const BigInt &b) {
  return BigInt{BzGcd(a.rep, b.rep), nullptr};
}

BigInt SqrtInternal(const BigInt &a);
BigInt BigInt::Sqrt(const BigInt &a) {
  // PERF: Bench bignum's native sqrt.
  return SqrtInternal(a);
}

std::pair<BigInt, BigInt> BigInt::SqrtRem(const BigInt &aa) {
  BigInt a = Sqrt(aa);
  BigInt aa1 = Times(a, a);
  return std::make_pair(a, Minus(aa, aa1));
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
BigRat &BigRat::operator =(BigRat &&other) noexcept {
  Swap(&other);
  return *this;
}

BigRat::BigRat(BigRat &&other) noexcept : BigRat() {
  // PERF: Should not actually need to create the rep only
  // to swap/free it.
  Swap(&other);
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
bool BigRat::Eq(const BigRat &a, int64_t b) {
  return Eq(a, BigRat(b));
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
BigRat BigRat::Negate(BigRat &&a) {
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

BigInt BigRat::Numerator() const {
  return BigInt(BzCopy(BqGetNumerator(rep)), nullptr);
}

BigInt BigRat::Denominator() const {
  return BigInt(BzCopy(BqGetDenominator(rep)), nullptr);
}

BigRat BigRat::FromDouble(double num) {
  assert(std::isfinite(num));

  if (num == 0.0) return BigRat(0);

  const uint64_t bits = std::bit_cast<uint64_t>(num);

  bool sign = !!(bits >> 63);
  // 11 exponent bits
  int exponent = (bits >> 52) & 0x7FF;
  // 52 fraction bits
  uint64_t fraction_bits = bits & uint64_t{0xFFFFFFFFFFFFFLL};

  uint64_t denom = uint64_t{1} << 52;

  // XXX test this more!
  uint64_t numerator = 0;
  if (exponent == 0) {
    // Subnormals
    numerator = fraction_bits;
    denom = 1;
    exponent -= (1023 + 52 - 1);
  } else {
    // Implied leading 1.
    numerator = (1ULL << 52) | fraction_bits;
    exponent -= 1023;
  }

  /*
    printf("numer %llu, denom %llu, exp %d\n",
    numerator, denom, exponent);
  */
  if (exponent > 0) {
    BigInt numer =
      BigInt::Times(BigInt(numerator),
                    BigInt::Pow(BigInt(2), exponent));
    if (sign) numer = BigInt::Negate(std::move(numer));
    return BigRat(numer, BigInt(denom));
  } else if (exponent < 0) {
    BigInt numer = BigInt(numerator);
    if (sign) numer = BigInt::Negate(std::move(numer));
    BigInt d =
      BigInt::Times(BigInt(denom),
                    BigInt::Pow(BigInt(2), -exponent));
    return BigRat(numer, d);
  } else {
    BigRat r = BigRat(numerator, denom);
    if (sign) return BigRat::Negate(std::move(r));
    return r;
  }
}

BigRat BigRat::ApproxDouble(double num, int64_t max_denom) {
  return BigRat{BqFromDouble(num, max_denom), nullptr};
}

double BigRat::ToDouble() const {
  return ToDoubleIterative(*this);
}

uint64_t BigRat::HashCode(const BigRat &a) {
  const auto &nrep = BqGetNumerator(a.rep);
  const auto &drep = BqGetDenominator(a.rep);
  uint64_t h = 0xC0FFEE'777'1234567;

  if (BzNumDigits(drep) != 0) {
    h ^= BzGetDigit(drep, 0);
    h = std::rotl<uint64_t>(h, 51);
    h *= uint64_t{6364136223846793005};
  }

  if (BzNumDigits(nrep) != 0) {
    // Sign gets stored in numerator.
    if (BzGetSign(nrep) == BZ_MINUS) {
      h ^= h >> 18;
    }

    h += BzGetDigit(nrep, 0);
  }

  return h;
}

int BigRat::Sign(const BigRat &a) {
  static_assert(BZ_MINUS == -1);
  static_assert(BZ_ZERO == 0);
  static_assert(BZ_PLUS == 1);
  return BzGetSign(BqGetNumerator(a.rep));
}

#endif

// Common / derived implementations.

bool BigInt::IsZero(const BigInt &a) {
  return Sign(a) == 0;
}

BigRat BigRat::Pow(const BigRat &a, uint64_t exponent) {
  const auto &[numer, denom] = a.Parts();
  BigInt nn(BigInt::Pow(numer, exponent));
  BigInt dd(BigInt::Pow(denom, exponent));
  return BigRat(nn, dd);
}

BigRat BigRat::Min(const BigRat &a, const BigRat &b) {
  const int cmp = BigRat::Compare(a, b);
  return cmp == 1 ? b : a;
}

BigRat BigRat::Max(const BigRat &a, const BigRat &b) {
  const int cmp = BigRat::Compare(a, b);
  return cmp == -1 ? b : a;
}

bool BigRat::Less(const BigRat &a, const BigRat &b) {
  return Compare(a, b) == -1;
}
bool BigRat::LessEq(const BigRat &a, const BigRat &b) {
  return Compare(a, b) != 1;
}
bool BigRat::Greater(const BigRat &a, const BigRat &b) {
  return Compare(a, b) == 1;
}
bool BigRat::GreaterEq(const BigRat &a, const BigRat &b) {
  return Compare(a, b) != -1;
}

bool BigRat::IsZero(const BigRat &a) {
  return Sign(a) == 0;
}

BigInt BigRat::Floor(const BigRat &r) {
  const auto &[numer, denom] = r.Parts();
  return BigInt::DivFloor(numer, denom);
}

BigInt BigRat::Ceil(const BigRat &r) {
  return BigInt::Negate(Floor(BigRat::Negate(r)));
}

#endif
