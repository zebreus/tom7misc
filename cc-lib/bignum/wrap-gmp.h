
// This is an implementation detail.
// Just include big.h.

#ifndef _CC_LIB_BIGNUM_WRAP_GMP_H
#define _CC_LIB_BIGNUM_WRAP_GMP_H

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
#include <optional>
#include <stdlib.h>
#include <string>
#include <string_view>
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
      memcpy(mpz, other.mpz, sizeof(mpz_t));

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
      memset(other.mpz, 0, sizeof(mpz_t));
    }

    Lease &operator=(Lease &&other) noexcept {
      if (this == &other) return *this;

      if (mpz->_mp_d != nullptr) {
        mpz_clear(mpz);
        mpz->_mp_d = nullptr;
      }

      memcpy(mpz, other.mpz, sizeof(mpz_t));

      if (other.ptr == other.mpz) {
        ptr = mpz;
      } else {
        ptr = other.ptr;
      }

      other.ptr = nullptr;
      memset(other.mpz, 0, sizeof(mpz_t));

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
