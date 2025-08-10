
// This is an implementation detail.
// Just include big.h.

#ifndef _CC_LIB_BIGNUM_WRAP_GMP_H
#define _CC_LIB_BIGNUM_WRAP_GMP_H

#include <limits>
#ifdef BIG_USE_GMP

#include <gmp.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <numeric>
#include <optional>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// mpz does not have a good representation for small integers;
// it performs an allocation even to store zero! We pierce
// the veil a little bit here. When the limbs pointer is null,
// then we are storing our own integer.

// In progress and experimental!

#define BIGNUM_SMALL_INT_OPTIMIZATION 1

#if BIGNUM_SMALL_INT_OPTIMIZATION

static_assert(offsetof(MP_INT, _mp_d) >= 8,
              "Need space for an int64_t at the beginning of "
              "the union.");
static_assert(sizeof (int64_t) == 8);

struct GmpRep {
  union {
    MP_INT mpz[1];
    int64_t small_int;
  } u;

  GmpRep() {
    // zero initialized represents zero; small_int is 0
    // and _mp_d is null, indicating IsSmall.
    memset(u.mpz, 0, sizeof (MP_INT));
  }

  GmpRep(const GmpRep &other) {
    if (other.IsSmall()) {
      memset(u.mpz, 0, sizeof (MP_INT));
      u.small_int = other.u.small_int;
    } else {
      mpz_init(u.mpz);
      mpz_set(u.mpz, other.u.mpz);
    }
  }

  GmpRep(int64_t small) {
    memset(u.mpz, 0, sizeof (MP_INT));
    u.small_int = small;
  }

  GmpRep(GmpRep &&other) noexcept {
    // We avoid initializing this; we'll just take ownership
    // of other's allocation. So this is just a byte-for-byte
    // copy.
    memcpy((void*)this, &other, sizeof (GmpRep));
    other.Release();
  }

  GmpRep &operator =(const GmpRep &other) {
    // Self-assignment does nothing.
    if (this == &other) return *this;
    if (other.IsSmall()) {
      // TODO PERF: Since we already have an allocation,
      // it could make sense to keep it?
      Demote();
      u.small_int = other.u.small_int;
    } else {
      if (IsSmall()) {
        mpz_init(u.mpz);
      }
      mpz_set(u.mpz, other.u.mpz);
    }
    return *this;
  }

  GmpRep &operator =(GmpRep &&other) noexcept {
    if (this == &other) return *this;

    if (!IsSmall()) {
      Demote();
    }

    memcpy((void*)this, &other, sizeof (GmpRep));

    other.Release();
    return *this;
  }

  void Swap(GmpRep *other) {
    // byte-wise swap, which doesn't care about
    // the combination of small/big
    unsigned char tmp[sizeof (GmpRep)];
    memcpy(tmp, (void*)this, sizeof (GmpRep));
    memcpy((void*)this, (void*)other, sizeof (GmpRep));
    memcpy((void*)other, tmp, sizeof (GmpRep));
  }

  bool IsSmall() const {
    return u.mpz->_mp_d == nullptr;
  }

  int64_t GetSmall() const {
    return u.small_int;
  }

  void Promote() {
    if (IsSmall()) {
      const int64_t small = u.small_int;
      mpz_init(u.mpz);
      MpzSetI64(u.mpz, small);
    }
  }

  MP_INT *Mpz() {
    Promote();
    return u.mpz;
  }

  // Precondition: !IsSmall.
  const MP_INT *ConstMpz() const {
    if (IsSmall()) {
      abort();
    }
    return u.mpz;
  }

  ~GmpRep() {
    Demote();
  }

  // As a stopgap for porting purposes, makes a copy of the rep that
  // is guaranteed to have an allocation. This allows thread-safe
  // for const functions that can only work with the mpz representation.
  // PERF: We should just avoid using this, but if we already have an
  // alloc we could use a reference to it instead. Could use "lease"
  // wrapper.
  GmpRep PromoteCopy() const {
    GmpRep tmp = *this;
    tmp.Promote();
    return tmp;
  }

  // Lease to a const mpz object.
  // This is used when we need an mpz to run a native GMP function,
  // but we only have a const GmpRep, which might be small. If it is
  // small, we allocate a temporary mpz and it is owned by this object
  // (and cleaned up by its destructor). Otherwise, it just wraps
  // a pointer to the existing allocation.
  struct Lease {
    explicit Lease(const GmpRep &rep) {
      if (rep.IsSmall()) {
        mpz_init(mpz);
        MpzSetI64(mpz, rep.GetSmall());
        ptr = mpz;
      } else {
        ptr = rep.ConstMpz();
        memset(mpz, 0, sizeof (MP_INT));
      }
    }

    // (We could implement a move constructor, which might
    // make sense for regularity in some cases?)

    const MP_INT *ConstMpz() const {
      return ptr;
    }

    ~Lease() {
      if (mpz->_mp_d != nullptr) {
        mpz_clear(mpz);
      }
      mpz->_mp_d = nullptr;
    }

    Lease(Lease &&other) noexcept {
      // Take ownership of the allocation, if any.
      memcpy(mpz, other.mpz, sizeof (mpz_t));

      // But, tricky: If the pointer was internal,
      // we need it to now point to our data.
      if (other.ptr == other.mpz) {
        // Point to *my* copy.
        ptr = mpz;
      } else {
        ptr = other.ptr;
      }

      // Makes sure other does not deallocate.
      other.ptr = nullptr;
      memset(other.mpz, 0, sizeof (mpz_t));
    }

    Lease &operator=(Lease &&other) noexcept {
      if (this == &other) return *this;

      if (mpz->_mp_d != nullptr) {
        mpz_clear(mpz);
        mpz->_mp_d = nullptr;
      }

      memcpy(mpz, other.mpz, sizeof (mpz_t));

      if (other.ptr == other.mpz) {
        ptr = mpz;
      } else {
        ptr = other.ptr;
      }

      other.ptr = nullptr;
      memset(other.mpz, 0, sizeof (mpz_t));

      return *this;
    }

   private:
    friend struct GmpRep;
    // Move-only!
    Lease() = delete;
    Lease(const Lease &other) = delete;
    void operator=(const Lease &other) = delete;

    // Might be a pointer into exiting object, or might
    // be a pointer to the next field.
    const MP_INT *ptr = nullptr;
    // Only initialized if necessary.
    // If uninitialized, its _mp_d field will be null.
    mpz_t mpz;
  };

  Lease GetLease() const {
    return Lease(*this);
  }


 private:

  // Demote to small int (with an arbitrary value), releasing any
  // allocation. If already small, does nothing.
  void Demote() {
    if (!IsSmall()) {
      mpz_clear(u.mpz);
      u.mpz->_mp_d = nullptr;
    }
  }

  // Release ownership of the allocation (if any) without freeing.
  // Note that when this is called, there is typically another
  // copy of the object (which is taking ownership of the alloc).
  void Release() {
    u.mpz->_mp_d = nullptr;
  }

  static void MpzSetI64(MP_INT *mpz, int64_t i) {
    if (i < 0) {
      // Note: Most negative value cannot be negated!
      uint64_t u = std::bit_cast<uint64_t>(i);
      // Manual two's complement negation.
      u = ~u + 1;
      MpzSetU64(mpz, u);
      mpz_neg(mpz, mpz);
    } else {
      MpzSetU64(mpz, (uint64_t)i);
    }
  }

  static void MpzSetU64(MP_INT *mpz, uint64_t u) {
    // Need to be able to set 4 bytes at a time.
    static_assert(sizeof (unsigned long int) >= 4);
    const uint32_t hi = 0xFFFFFFFF & (u >> 32);
    const uint32_t lo = 0xFFFFFFFF & u;
    mpz_set_ui(mpz, hi);
    mpz_mul_2exp(mpz, mpz, 32);
    mpz_add_ui(mpz, mpz, lo);
  }
};



// Same idea, but for mpq (rationals).
struct GmpRepRat {
 private:
  // Same approach to layout as the union in bigint, but we access the
  // fields through the mpq in the union below. This union is just to
  // make sure we have the same alignment as the numerator and
  // denominator in MP_RAT.
  union Component {
    unsigned char padding[sizeof (MP_INT)];
    int64_t small_int;
  };

  static_assert(sizeof (Component) == sizeof (MP_INT));

  // We use the numerator's mp_d pointer to indicate whether
  // there is an allocation. If it is non-null, then both
  // numerator and denominator are allocated. If it is null,
  // then this is the small representation, and the denominator's
  // mp_d pointer is meaningless (note that it might be a
  // dangling non-null pointer in that case).
  //
  // Like GMP, we keep numer/denom in canonical form. If the numerator
  // is zero, then the denominator is ignored. Otherwise, the denominator
  // is positive. The two numbers are relatively prime (reduced fraction).
  union U {
    MP_RAT mpq[1];
    struct S {
      Component numer;
      Component denom;
    } s;
  } u;
 public:

  GmpRepRat() {
    // note: 0/0 is valid representation for zero.
    memset(u.mpq, 0, sizeof (MP_RAT));
  }

  GmpRepRat(const GmpRepRat &other) {
    if (other.IsSmall()) {
      memset(u.mpq, 0, sizeof (MP_RAT));
      u.s.numer.small_int = other.u.s.numer.small_int;
      u.s.denom.small_int = other.u.s.denom.small_int;
    } else {
      mpq_init(u.mpq);
      mpq_set(u.mpq, other.u.mpq);
    }
  }

  GmpRepRat(int64_t small) {
    memset(u.mpq, 0, sizeof (MP_RAT));
    u.s.numer.small_int = small;
    u.s.denom.small_int = 1;
  }

  GmpRepRat(int64_t n, int64_t d) {
    memset(u.mpq, 0, sizeof (MP_RAT));

    assert(d != 0);

    // Can't handle the most negative number, since it has no
    // negation. (Even calling std::gcd on it is undefined behavior.)
    // Just eagerly promote if we're in this case.
    if (d == std::numeric_limits<int64_t>::lowest() ||
        n == std::numeric_limits<int64_t>::lowest()) [[unlikely]] {

      u.s.numer.small_int = n;
      u.s.denom.small_int = d;

      Promote();
      return;
    }

    if (d < 0) {
      n = -n;
      d = -d;
    }

    // Now the sign is in the numerator. Put in canonical
    // form (no common factors).

    const int64_t gcd = std::gcd(n, d);

    // Need to check for zero anyway, so we can also avoid the
    // division if they are already relatively prime.
    if (gcd > 1) {
      n /= gcd;
      d /= gcd;
    }

    u.s.numer.small_int = n;
    u.s.denom.small_int = d;
  }

  GmpRepRat(GmpRepRat &&other) noexcept {
    // We avoid initializing this; we'll just take ownership
    // of other's allocation. So this is just a byte-for-byte
    // copy.
    memcpy((void*)this, &other, sizeof (GmpRepRat));
    other.Release();
  }

  GmpRepRat &operator =(const GmpRepRat &other) {
    // Self-assignment does nothing.
    if (this == &other) return *this;
    if (other.IsSmall()) {
      // TODO PERF: Since we already have an allocation,
      // it could make sense to keep it?
      Demote();
      u.s.numer.small_int = other.u.s.numer.small_int;
      u.s.denom.small_int = other.u.s.denom.small_int;
    } else {
      if (IsSmall()) {
        mpq_init(u.mpq);
      }
      mpq_set(u.mpq, other.u.mpq);
    }
    return *this;
  }

  GmpRepRat &operator =(GmpRepRat &&other) noexcept {
    if (this == &other) return *this;

    if (!IsSmall()) {
      Demote();
    }

    memcpy((void*)this, &other, sizeof (GmpRepRat));

    other.Release();
    return *this;
  }

  void Swap(GmpRepRat *other) {
    // byte-wise swap, which doesn't care about
    // the combination of small/big
    unsigned char tmp[sizeof (GmpRepRat)];
    memcpy(tmp, (void*)this, sizeof (GmpRepRat));
    memcpy((void*)this, (void*)other, sizeof (GmpRepRat));
    memcpy((void*)other, tmp, sizeof (GmpRepRat));
  }

  bool IsSmall() const {
    // The numerator is used to detect small.
    // The numerator and denominator are always either both
    // small or both allocated.
    return u.mpq->_mp_num._mp_d == nullptr;
  }

  // Precondition: IsSmall()
  int64_t SmallNumer() const {
    return u.s.numer.small_int;
  }

  // Precondition: IsSmall()
  int64_t SmallDenom() const {
    return u.s.denom.small_int;
  }

  void Promote() {
    if (IsSmall()) {
      // PERF: If we guarantee keeping these in canonical form,
      // then we can set without canonicalizing.
      const int64_t n = u.s.numer.small_int;
      const int64_t d = u.s.denom.small_int;
      mpq_init(u.mpq);
      // It begins zeroed. We don't set if numerator is zero,
      // because if the denominator is also zero, then it incurs
      // a divide by zero trap.
      if (n) {
        MpqSetI64s(u.mpq, n, d);
      }
    }
  }

  MP_RAT *Mpq() {
    Promote();
    return u.mpq;
  }

  // Precondition: !IsSmall.
  const MP_RAT *ConstMpq() const {
    if (IsSmall()) {
      abort();
    }
    return u.mpq;
  }

  ~GmpRepRat() {
    Demote();
  }

  // Lease to a const mpq object, just like in GmpRep.
  struct Lease {
    explicit Lease(const GmpRepRat &rep) {
      if (rep.IsSmall()) {
        mpq_init(mpq);
        MpqSetI64s(mpq, rep.SmallNumer(), rep.SmallDenom());
        ptr = mpq;
      } else {
        ptr = rep.ConstMpq();
        memset(mpq, 0, sizeof (MP_RAT));
      }
    }

    // (We could implement a move constructor, which might
    // make sense for regularity in some cases?)

    const MP_RAT *ConstMpq() const {
      return ptr;
    }

    ~Lease() {
      if (mpq->_mp_num._mp_d != nullptr) {
        mpq_clear(mpq);
      }
      mpq->_mp_num._mp_d = nullptr;
    }

    Lease(Lease &&other) noexcept {
      // Take ownership of the allocation, if any.
      memcpy(mpq, other.mpq, sizeof (mpq_t));

      // But, tricky: If the pointer was internal,
      // we need it to now point to our data.
      if (other.ptr == other.mpq) {
        // Point to *my* copy.
        ptr = mpq;
      } else {
        ptr = other.ptr;
      }

      // Makes sure other does not deallocate.
      other.ptr = nullptr;
      memset(other.mpq, 0, sizeof (mpq_t));
    }

    Lease &operator=(Lease &&other) noexcept {
      if (this == &other) return *this;

      // If overwriting an allocation, clear it.
      if (mpq->_mp_num._mp_d != nullptr) {
        mpq_clear(mpq);
        mpq->_mp_num._mp_d = nullptr;
      }

      memcpy(mpq, other.mpq, sizeof (mpq_t));

      if (other.ptr == other.mpq) {
        ptr = mpq;
      } else {
        ptr = other.ptr;
      }

      other.ptr = nullptr;
      memset(other.mpq, 0, sizeof (mpq_t));

      return *this;
    }

   private:
    friend struct GmpRepRat;
    // Move-only!
    Lease() = delete;
    Lease(const Lease &other) = delete;
    void operator=(const Lease &other) = delete;

    // Might be a pointer into exiting object, or might
    // be a pointer to the next field.
    const MP_RAT *ptr = nullptr;
    // Only initialized if necessary.
    // If uninitialized, its numerator's _mp_d field will be null.
    mpq_t mpq;
  };

  Lease GetLease() const {
    return Lease(*this);
  }

 private:
  friend struct GmpRat_Internal_Assert;

  // Demote to small int (with an arbitrary value), releasing any
  // allocation. If already small, does nothing.
  void Demote() {
    if (!IsSmall()) {
      mpq_clear(u.mpq);
      u.mpq[0]._mp_num._mp_d = nullptr;
    }
  }

  // Release ownership of the allocation (if any) without freeing.
  // Note that when this is called, there is typically another
  // copy of the object (which is taking ownership of the alloc).
  // Makes the object "small" with an arbitrary value.
  void Release() {
    u.mpq[0]._mp_num._mp_d = nullptr;
  }

  // TODO PERF: Version that assumes we're already canonicalized.
  static void MpqSetI64s(MP_RAT *mpq, int64_t n, int64_t d) {
    MpzSetI64(&mpq->_mp_num, n);
    MpzSetI64(&mpq->_mp_den, d);
    mpq_canonicalize(mpq);
  }

  static void MpzSetI64(MP_INT *mpz, int64_t i) {
    if (i < 0) {
      // Note: Most negative value cannot be negated!
      uint64_t u = std::bit_cast<uint64_t>(i);
      // Manual two's complement negation.
      u = ~u + 1;
      MpzSetU64(mpz, u);
      mpz_neg(mpz, mpz);
    } else {
      MpzSetU64(mpz, (uint64_t)i);
    }
  }

  static void MpzSetU64(MP_INT *mpz, uint64_t u) {
    // Need to be able to set 4 bytes at a time.
    static_assert(sizeof (unsigned long int) >= 4);
    const uint32_t hi = 0xFFFFFFFF & (u >> 32);
    const uint32_t lo = 0xFFFFFFFF & u;
    mpz_set_ui(mpz, hi);
    mpz_mul_2exp(mpz, mpz, 32);
    mpz_add_ui(mpz, mpz, lo);
  }
};

// Need access to private members to do these assertions, and
// we can't do them before the class definition is complete. So
// we have a friend helper that you can ignore:
struct GmpRat_Internal_Assert {
 private:
  GmpRat_Internal_Assert() = delete;
  static_assert(offsetof(GmpRepRat, u.mpq[0]._mp_num) ==
                offsetof(GmpRepRat, u.s.numer));
  static_assert(offsetof(GmpRepRat, u.mpq[0]._mp_den) ==
                offsetof(GmpRepRat, u.s.denom), "Sorry, this requires "
                "matching the representation of MP_RAT!");
  static_assert(alignof (GmpRepRat) == alignof (MP_RAT),
                "Alignment mismatch!");

  // Not found??
  // static_assert(std::is_layout_compatible<GmpRat::U::S, MP_RAT>::value);
};

#else

#error (expect this to be disabled)

struct GmpRep {
  union {
    MP_INT mpz[1];
  } u;

  // TODO: Move Constructors, assignment

  constexpr bool IsSmall() const {
    return false;
  }

  int64_t GetSmall() const {
    abort();
  }

  GmpRep() {
    mpz_init(u.mpz);
  }

  GmpRep(const GmpRep &other) {
    mpz_init(u.mpz);
    mpz_set(u.mpz, other.u.mpz);
  }

  Swap(GmpRep *other) {
    mpz_swap(u.mpz, other->u.mpz);
  }

  void Promote() {}

  MP_INT *Mpz() {
    return u.mpz;
  }

  ~GmpRep() {
    mpz_clear(u.mpz);
    u.mpz->_mp_d = nullptr;
  }
};

#endif


#endif
#endif
