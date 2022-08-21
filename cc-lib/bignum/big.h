
// C++ wrappers for big integers and rationals by tom7 for cc-lib.

#ifndef _CC_LIB_BIGNUM_BIG_H
#define _CC_LIB_BIGNUM_BIG_H

#include "bigz.h"
#include "bign.h"
#include "bigq.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <optional>

struct BigInt {
  BigInt() : BigInt(0LL) {}
  inline explicit BigInt(int64_t n);
  inline explicit BigInt(const std::string &digits);

  // Value semantics with linear-time copies (like std::vector).
  inline BigInt(const BigInt &other);
  inline BigInt &operator =(const BigInt &other);
  inline BigInt &operator =(BigInt &&other);

  inline ~BigInt();

  inline std::string ToString(int base = 10) const;

  inline bool IsEven() const;
  inline bool IsOdd() const;

  inline static BigInt Negate(const BigInt &a);
  inline static BigInt Abs(const BigInt &a);
  inline static int Compare(const BigInt &a, const BigInt &b);
  inline static bool Less(const BigInt &a, const BigInt &b);
  inline static bool LessEq(const BigInt &a, const BigInt &b);
  inline static bool Eq(const BigInt &a, const BigInt &b);
  inline static bool Greater(const BigInt &a, const BigInt &b);
  inline static bool GreaterEq(const BigInt &a, const BigInt &b);
  inline static BigInt Plus(const BigInt &a, const BigInt &b);
  inline static BigInt Minus(const BigInt &a, const BigInt &b);
  inline static BigInt Times(const BigInt &a, const BigInt &b);
  inline static BigInt Div(const BigInt &a, const BigInt &b);
  inline static BigInt Mod(const BigInt &a, const BigInt &b);
  inline static BigInt Pow(const BigInt &a, uint64_t exponent);
  // Returns Q (a div b), R (a mod b) such that a = b * q + r
  inline static std::pair<BigInt, BigInt> QuotRem(const BigInt &a,
                                                  const BigInt &b);
  inline std::optional<int64_t> ToInt() const;

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

  inline void Swap(BigInt *other);

private:
  friend class BigRat;
  // Takes ownership.
  // nullptr token here is just used to distinguish from the version
  // that takes an int64 (would be ambiguous with BigInt(0)).
  explicit BigInt(BigZ z, nullptr_t token) : bigz(z) {}

  // BigZ is a pointer to a bigz struct, which is the
  // header followed by digits.
  BigZ bigz = nullptr;
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
  // Takes ownership.
  // Token for disambiguation as above.
  explicit BigRat(BigQ q, nullptr_t token) : bigq(q) {}
  // TODO: This is a pointer to a struct with two BigZs (pointers),
  // so it would probably be much better to just unpack it here.
  // bigq.cc is seemingly set up to do this by redefining some
  // macros in the EXTERNAL_BIGQ_MEMORY section of the header.
  BigQ bigq = nullptr;
};


// Implementations follow. These are all light wrappers around
// bigz/bigq functions, so inline makes sense.

BigInt::BigInt(int64_t n) : bigz(BzFromInteger(n)) { }

BigInt::BigInt(const BigInt &other) : bigz(BzCopy(other.bigz)) { }
BigInt &BigInt::operator =(const BigInt &other) {
  // Self-assignment does nothing.
  if (this == &other) return *this;
  BzFree(bigz);
  bigz = BzCopy(other.bigz);
  return *this;
}
BigInt &BigInt::operator =(BigInt &&other) {
  // We don't care how we leave other, but it needs to be valid (e.g. for
  // the destructor). Swap is a good way to do this.
  Swap(&other);
  return *this;
}

BigInt::~BigInt() {
  BzFree(bigz);
  bigz = nullptr;
}

void BigInt::Swap(BigInt *other) {
  std::swap(bigz, other->bigz);
}

BigInt::BigInt(const std::string &digits) {
  bigz = BzFromStringLen(digits.c_str(), digits.size(), 10, BZ_UNTIL_END);
}

std::string BigInt::ToString(int base) const {
  // Allocates a buffer.
  // Third argument forces a + sign for positive; not used here.
  BzChar *buf = BzToString(bigz, base, 0);
  std::string ret{buf};
  BzFreeString(buf);
  return ret;
}

std::optional<int64_t> BigInt::ToInt() const {
  if (BzNumDigits(bigz) > (BigNumLength)1) {
    return std::nullopt;
  } else {
    uint64_t digit = BzGetDigit(bigz, 0);
    // Would overflow int64. (This may be a bug in BzToInteger?)
    if (digit & 0x8000000000000000ULL)
      return std::nullopt;
    if (BzGetSign(bigz) == BZ_MINUS) {
      return {-(int64_t)digit};
    }
    return {(int64_t)digit};
  }
}


bool BigInt::IsEven() const { return BzIsEven(bigz); }
bool BigInt::IsOdd() const { return BzIsOdd(bigz); }

BigInt BigInt::Negate(const BigInt &a) {
  return BigInt{BzNegate(a.bigz), nullptr};
}
BigInt BigInt::Abs(const BigInt &a) {
  return BigInt{BzAbs(a.bigz), nullptr};
}
// TODO: Overload <, etc. and <=>
int BigInt::Compare(const BigInt &a, const BigInt &b) {
  switch (BzCompare(a.bigz, b.bigz)) {
  case BZ_LT: return -1;
  case BZ_EQ: return 0;
  default:
  case BZ_GT: return 1;
  }
}
bool BigInt::Less(const BigInt &a, const BigInt &b) {
  return BzCompare(a.bigz, b.bigz) == BZ_LT;
}
bool BigInt::LessEq(const BigInt &a, const BigInt &b) {
  auto cmp = BzCompare(a.bigz, b.bigz);
  return cmp == BZ_LT || cmp == BZ_EQ;
}
bool BigInt::Eq(const BigInt &a, const BigInt &b) {
  return BzCompare(a.bigz, b.bigz) == BZ_EQ;
}
bool BigInt::Greater(const BigInt &a, const BigInt &b) {
  return BzCompare(a.bigz, b.bigz) == BZ_GT;
}
bool BigInt::GreaterEq(const BigInt &a, const BigInt &b) {
  auto cmp = BzCompare(a.bigz, b.bigz);
  return cmp == BZ_GT || cmp == BZ_EQ;
}

BigInt BigInt::Plus(const BigInt &a, const BigInt &b) {
  return BigInt{BzAdd(a.bigz, b.bigz), nullptr};
}
BigInt BigInt::Minus(const BigInt &a, const BigInt &b) {
  return BigInt{BzSubtract(a.bigz, b.bigz), nullptr};
}
BigInt BigInt::Times(const BigInt &a, const BigInt &b) {
  return BigInt{BzMultiply(a.bigz, b.bigz), nullptr};
}
// TODO: Quotrem via BzDivide
BigInt BigInt::Div(const BigInt &a, const BigInt &b) {
  return BigInt{BzDiv(a.bigz, b.bigz), nullptr};
}
// TODO: truncate, floor, ceiling round. what are they?

// TODO: Clarify mod vs rem?
BigInt BigInt::Mod(const BigInt &a, const BigInt &b) {
  return BigInt{BzMod(a.bigz, b.bigz), nullptr};
}

// Returns Q (a div b), R (a mod b) such that a = b * q + r
std::pair<BigInt, BigInt> BigInt::QuotRem(const BigInt &a,
                                          const BigInt &b) {
  BigZ r;
  BigZ q = BzDivide(a.bigz, b.bigz, &r);
  return std::make_pair(BigInt{q, nullptr}, BigInt{r, nullptr});
}

BigInt BigInt::Pow(const BigInt &a, uint64_t exponent) {
  return BigInt{BzPow(a.bigz, exponent), nullptr};
}

BigRat::BigRat(int64_t numer, int64_t denom) {
  // PERF This could avoid creating intermediate BigZ with
  // a new function inside bigq.
  BigInt n{numer}, d{denom};
  bigq = BqCreate(n.bigz, d.bigz);
}
BigRat::BigRat(int64_t numer) : BigRat(numer, int64_t{1}) {}

BigRat::BigRat(const BigInt &numer, const BigInt &denom)
  : bigq(BqCreate(numer.bigz, denom.bigz)) {}

BigRat::BigRat(const BigInt &numer) : BigRat(numer, BigInt(1)) {}

// PERF: Should have BqCopy so that we don't need to re-normalize.
BigRat::BigRat(const BigRat &other) :
  bigq(BqCreate(
           BqGetNumerator(other.bigq),
           BqGetDenominator(other.bigq))) {
}
BigRat &BigRat::operator =(const BigRat &other) {
  // Self-assignment does nothing.
  if (this == &other) return *this;
  BqDelete(bigq);
  bigq = BqCreate(BqGetNumerator(other.bigq),
                  BqGetDenominator(other.bigq));
  return *this;
}
BigRat &BigRat::operator =(BigRat &&other) {
  Swap(&other);
  return *this;
}

void BigRat::Swap(BigRat *other) {
  std::swap(bigq, other->bigq);
}

BigRat::~BigRat() {
  BqDelete(bigq);
  bigq = nullptr;
}

int BigRat::Compare(const BigRat &a, const BigRat &b) {
  switch (BqCompare(a.bigq, b.bigq)) {
  case BQ_LT: return -1;
  case BQ_EQ: return 0;
  default:
  case BQ_GT: return 1;
  }
}

bool BigRat::Eq(const BigRat &a, const BigRat &b) {
  return BqCompare(a.bigq, b.bigq) == BQ_EQ;
}

BigRat BigRat::Abs(const BigRat &a) {
  return BigRat{BqAbs(a.bigq), nullptr};
}
BigRat BigRat::Div(const BigRat &a, const BigRat &b) {
  return BigRat{BqDiv(a.bigq, b.bigq), nullptr};
}
BigRat BigRat::Inverse(const BigRat &a) {
  return BigRat{BqInverse(a.bigq), nullptr};
}
BigRat BigRat::Times(const BigRat &a, const BigRat &b) {
  return BigRat{BqMultiply(a.bigq, b.bigq), nullptr};
}
BigRat BigRat::Negate(const BigRat &a) {
  return BigRat{BqNegate(a.bigq), nullptr};
}
BigRat BigRat::Plus(const BigRat &a, const BigRat &b) {
  BigQ res = BqAdd(a.bigq, b.bigq);
  return BigRat{res, nullptr};
}
BigRat BigRat::Minus(const BigRat &a, const BigRat &b) {
  return BigRat{BqSubtract(a.bigq, b.bigq), nullptr};
}
BigRat BigRat::Pow(const BigRat &a, uint64_t exponent) {
  BigZ numer = BqGetNumerator(a.bigq);
  BigZ denom = BqGetDenominator(a.bigq);
  BigInt nn(BzPow(numer, exponent), nullptr);
  BigInt dd(BzPow(denom, exponent), nullptr);
  return BigRat(nn, dd);
}

std::string BigRat::ToString() const {
  // No forced +
  BzChar *buf = BqToString(bigq, 0);
  std::string ret{buf};
  BzFreeString(buf);
  return ret;
}

std::pair<BigInt, BigInt> BigRat::Parts() const {
  return std::make_pair(BigInt(BzCopy(BqGetNumerator(bigq)), nullptr),
                        BigInt(BzCopy(BqGetDenominator(bigq)), nullptr));
}

BigRat BigRat::ApproxDouble(double num, int64_t max_denom) {
  return BigRat{BqFromDouble(num, max_denom), nullptr};
}

double BigRat::ToDouble() const {
  return BqToDouble(bigq);
}


#endif
