
#ifndef _SOS_BHASKARA_UTIL_H
#define _SOS_BHASKARA_UTIL_H

#include "bignum/big.h"
#include "bignum/big-overloads.h"

#include <cstdint>
#include <vector>
#include <utility>
#include <memory>
#include <unordered_set>

#include "hashing.h"
#include "base/logging.h"

struct Triple {
  Triple() : Triple(0, 0, 0) {}
  Triple(BigInt aa, BigInt bb, BigInt kk) : a(std::move(aa)),
                                            b(std::move(bb)),
                                            k(std::move(kk)) {}
  Triple(int64_t aa, int64_t bb, int64_t kk) : a(aa), b(bb), k(kk) {}
  inline void Swap(Triple *other) {
    a.Swap(&other->a);
    b.Swap(&other->b);
    k.Swap(&other->k);
  }

  Triple(const Triple &other) : a(other.a), b(other.b), k(other.k) {}

  Triple &operator =(const Triple &other) {
    // Self-assignment does nothing.
    if (this == &other) return *this;
    a = other.a;
    b = other.b;
    k = other.k;
    return *this;
  }
  Triple &operator =(Triple &&other) {
    // other must remain valid.
    Swap(&other);
    return *this;
  }

  BigInt a, b, k;
};

struct HashTriple {
  size_t operator()(const Triple &tri) const {
    return (size_t)(BigInt::LowWord(tri.k) * 0x314159 +
                    BigInt::LowWord(tri.a) * 0x7FFFFFFF +
                    BigInt::LowWord(tri.b));
  }
};

namespace std {
template <> struct hash<Triple> {
  size_t operator()(const Triple &tri) const {
    return HashTriple()(tri);
  }
};
}

static inline bool operator ==(const Triple &x, const Triple &y) {
  return x.k == y.k &&
    x.a == y.a &&
    x.b == y.b;
}

using TripleSet = std::unordered_set<Triple, HashTriple>;

using TriplePairHash = Hashing<std::pair<Triple, Triple>>;

using TriplePairSet = std::unordered_set<std::pair<Triple, Triple>,
                                         TriplePairHash>;

// Map a bigint to a reasonable range for plotting in graphics.
// (or a.ToDouble(), or some hybrid?)
static double MapBig(BigInt z) {
  if (z == 0) return 0.0;
  double sign = 1.0;
  if (z < BigInt{0}) {
    sign = -1.0;
    z = BigInt::Abs(z);
  }

  double d = sign * BigInt::NaturalLog(z);
  CHECK(!std::isnan(d));
  CHECK(std::isfinite(d));
  return d;
}

std::string LongNum(const BigInt &a);

void MakeImages(int64_t iters,
                const std::string &base, int image_idx,
                const std::vector<std::pair<Triple, Triple>> &history);

#endif
