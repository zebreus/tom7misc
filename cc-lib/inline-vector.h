
// Experimental!

#ifndef _CC_LIB_INLINE_VECTOR_H
#define _CC_LIB_INLINE_VECTOR_H

#include <memory>
#include <cstdlib>
#include <array>
#include <cstring>
#include <span>
#include <utility>
#include <type_traits>

#include "base/logging.h"

// XXX
#include "base/print.h"
#include "hexdump.h"

// Types that InlineVector can work on.
namespace internal {
template <typename T>
struct is_memcpy_safe : std::is_trivially_copyable<T> {};
// We explicitly allow pair, even though this is not "trivially copyable"
// in many implementations due to custom constructors. The implementation
// works around this by explicitly moving.
template <typename A, typename B>
struct is_memcpy_safe<std::pair<A, B>>
    : std::bool_constant<is_memcpy_safe<A>::value && is_memcpy_safe<B>::value> {};
template <typename T>
inline constexpr bool is_memcpy_safe_v = is_memcpy_safe<T>::value;
}  // internal

// Like std::vector<T> but with the possibility of storing a
// small number of elements inline. Only works for POD-type
// T, like a pointer or integer.
template<class T>
struct InlineVector {
  static_assert(internal::is_memcpy_safe_v<T>,
                "T must be blessed as memcpy-able (e.g. by being "
                "trivially copyable)");
  static_assert(std::is_trivially_destructible_v<T>, "T must be trivially destructible");

  static constexpr bool VERBOSE = false;

  // Value semantics.
  inline InlineVector();
  inline InlineVector(size_t n, T init = T());
  inline InlineVector(const InlineVector &other);
  inline InlineVector &operator=(const InlineVector &other);
  inline InlineVector(InlineVector &&other);
  inline InlineVector &operator=(InlineVector &&other);
  inline ~InlineVector();

  void reserve(size_t n) {
    if (n > capacity()) {
      EnsureAlloc(n);
    }
  }

  size_t size() const {
    return GetSize(SizeField());
  }

  bool empty() const {
    return size() == 0;
  }

  void clear() {
    SetSize(0);
  }

  T &back() {
    DCHECK(!empty());
    return (*this)[size() - 1];
  }

  const T &back() const {
    DCHECK(!empty());
    return (*this)[size() - 1];
  }

  void pop_back() {
    DCHECK(!empty());
    SetSize(size() - 1);
  }

  size_t capacity() const {
    if (HasAlloc()) {
      return u.ar.reserved;
    } else {
      return MAX_INLINE;
    }
  }

  inline void push_back(T t);

  template <typename... Args>
  T& emplace_back(Args&&... args) {
    push_back(T(std::forward<Args>(args)...));
    return back();
  }

  // TODO: Insert

  inline T *data();
  inline const T *data() const;

  const T &operator [](size_t idx) const {
    return data()[idx];
  }

  T &operator [](size_t idx) {
    return data()[idx];
  }

  bool operator==(const InlineVector &other) const {
    const size_t n = size();
    if (n != other.size()) return false;
    for (size_t i = 0; i < n; i++)
      if ((*this)[i] != other[i])
        return false;
    return true;
  }

  using const_iterator = const T*;
  using iterator = T*;

  const_iterator begin() const { return data(); }
  const_iterator end() const { return data() + size(); }
  iterator begin() { return data(); }
  iterator end() { return data() + size(); }

  static constexpr size_t MaxInline() { return MAX_INLINE; }

 private:
  // Our target is to use up to one cache line.
  // Multiples of 64 should work great here, but if this
  // is not a multiple of standard alignments, you might need
  // to fiddle with the MAX_INLINE calculation.
  static constexpr size_t BYTES = 64;

  // Compute the number of T elements (properly aligned, after the
  // size field) that we can fit within the target size. We require
  // at least one.
  static constexpr size_t ALIGNED_OFFSET = (sizeof(size_t) + alignof(T) - 1) & ~(alignof(T) - 1);
  static constexpr size_t MAX_INLINE =
    std::max<size_t>(1, (BYTES - ALIGNED_OFFSET) / sizeof(T));
  static_assert(MAX_INLINE > 0);

  // When externally allocated, we have a bog-standard vector.
  // size is the number of elements actually stored, and reserved
  // is the total space available in the alloc.
  struct AllocRep {
    // First element of the two representations is shared.
    // When size < ALLOC_SIZE we always use InlineRep.
    size_t size;
    size_t reserved;
    T *alloc;
  };

  struct InlineRep {
    size_t size;
    std::array<T, MAX_INLINE> buf;
  };

  static_assert(sizeof (AllocRep) <= BYTES, "The allocated representation "
                "should be smaller than the target size, or else we're "
                "missing an opportunity to store more inline");

  inline bool HasAlloc() const {
    return GetAllocated(SizeField());
  }

  static constexpr int ALLOCATED_BIT = sizeof (size_t) * 8 - 1;
  static constexpr size_t SIZE_MASK = ~(size_t{1u} << ALLOCATED_BIT);

  // Read the shared size field (in either representation).
  size_t &SizeField() { return u.ar.size; }
  size_t SizeField() const { return u.ar.size; }

  // Assumes we already have capacity (e.g. with EnsureAlloc).
  void SetSize(size_t new_size) {
    DCHECK(new_size <= capacity());
    SizeField() = MakeSizeField(new_size, HasAlloc());
  }

  static constexpr size_t GetSize(size_t size_field) {
    return size_field & SIZE_MASK;
  }

  static constexpr bool GetAllocated(size_t size_field) {
    return !!(size_field >> ALLOCATED_BIT);
  }

  static constexpr size_t MakeSizeField(size_t size, bool allocated) {
    DCHECK((size & SIZE_MASK) == size);
    return size | (allocated ? (size_t{1u} << ALLOCATED_BIT) : 0);
  }

  // Ensure that we have reserved at least this many elements.
  // If resizing, it will allocate with capacity of exactly n.
  // n must be at least as large as the current size.
  void EnsureAlloc(size_t n) {
    if (capacity() < n) {
      const size_t current_size = size();
      // Must have grown by at least one, since n is strictly
      // larger than the capacity, and size is bounded by
      // capacity.
      DCHECK(n > current_size);
      // We always have at least this amount reserved, since
      // the small representation reserves it in place.
      DCHECK(n > MAX_INLINE);
      // PERF: Consider realloc if we already have an alloc.
      T *new_alloc = (T*)malloc(n * sizeof (T));
      if (current_size > 0) {
        if constexpr (VERBOSE)
          Print("Copy cur {} elts to {} from {}\n",
                current_size, (void*)new_alloc, (void*)data());
        // Like memcpy, but avoids strict UB on e.g. pair<int, int>.
        std::uninitialized_move_n(data(), current_size, new_alloc);
      }

      if (HasAlloc()) {
        free(u.ar.alloc);
        u.ar.alloc = nullptr;
      }

      // Now we definitely have an allocation.
      u.ar.size = MakeSizeField(current_size, true);
      u.ar.reserved = n;
      u.ar.alloc = new_alloc;
      if constexpr (VERBOSE)
        Print("Alloc reserved {} with contents:\n{}\n",
              u.ar.reserved,
              HexDump::Color(std::span<const uint8_t>((const uint8_t*)new_alloc,
                                                      sizeof (T) * current_size)));

      DCHECK(HasAlloc());
    }
  }

  // using InlineRep = internal::InlineRep<T, MAX_INLINE>;

  // The size field must be the same for each member.
  // Its high bit is 1 if we have an allocation.
  union {
    AllocRep ar;
    InlineRep ir;
  } u;
};


// Template implementations follow.

template<class T>
InlineVector<T>::InlineVector() : u{.ar = {0, 0, nullptr}} {
  #ifndef NDEBUG
  memset((void*)&u, 0, sizeof (u));
  #endif
  SizeField() = MakeSizeField(0, false);
}

template<class T>
InlineVector<T>::InlineVector(size_t n, T init) :
  InlineVector() {
  EnsureAlloc(n);
  SetSize(n);
  T *d = data();
  for (size_t i = 0; i < n; i++) {
    // d[i] = init;
    std::construct_at(d + i, init);
  }
}

template<class T>
InlineVector<T>::InlineVector(const InlineVector &other) : InlineVector() {
  const size_t n = other.size();
  EnsureAlloc(n);
  SetSize(n);
  std::uninitialized_copy_n(other.data(), n, data());
  // memcpy(data(), other.data(), sizeof (T) * n);
}

template<class T>
auto InlineVector<T>::operator=(const InlineVector &other) -> InlineVector & {
  if (this == &other) return *this;
  const size_t n = other.size();
  EnsureAlloc(n);
  SetSize(n);
  std::uninitialized_copy_n(other.data(), n, data());
  // memcpy(data(), other.data(), sizeof (T) * n);
  return *this;
}

template<class T>
InlineVector<T>::InlineVector(InlineVector &&other) : InlineVector() {
  if constexpr (std::is_trivially_copyable_v<T>) {
    // Take over the alloc, if any; either way the representation
    // is the same.
    memcpy((void*)&u, &other.u, sizeof (u));
  } else {
    // Otherwise, be explicit to avoid strict UB. This is a little
    // worse because of the branch and the fact that the copy of
    // the size field maybe can't be folded into the memcpy.
    if (other.HasAlloc()) {
      u.ar = other.u.ar;
    } else {
      SizeField() = other.SizeField();
      std::uninitialized_move_n(other.data(), other.size(), data());
    }
  }
  #ifndef NDEBUG
  memset((void*)&other.u, 0, sizeof (u));
  #endif
  other.SizeField() = MakeSizeField(0, false);
}

template<class T>
auto InlineVector<T>::operator=(InlineVector &&other) -> InlineVector & {
  if (this == &other) return *this;

  if (HasAlloc()) {
    free(u.ar.alloc);
    u.ar.alloc = nullptr;
  }

  // As above.
  if constexpr (std::is_trivially_copyable_v<T>) {
    memcpy((void*)&u, &other.u, sizeof (u));
  } else {
    if (other.HasAlloc()) {
      u.ar = other.u.ar;
    } else {
      SizeField() = other.SizeField();
      std::uninitialized_move_n(other.data(), other.size(), data());
    }
  }
  #ifndef NDEBUG
  memset((void*)&other.u, 0, sizeof (u));
  #endif
  other.SizeField() = MakeSizeField(0, false);
  return *this;
}

template<class T>
InlineVector<T>::~InlineVector() {
  if (HasAlloc()) {
    free(u.ar.alloc);
    u.ar.alloc = nullptr;
  }
  SizeField() = MakeSizeField(0, false);
}


template<class T>
void InlineVector<T>::push_back(T t) {
  const size_t cur = size();
  size_t cap = capacity();

  if (cur == cap) {
    // When reaching capacity, grow by a factor of 1.5.
    size_t new_cap = cap + (cap >> 1);
    if constexpr (MAX_INLINE == 1) {
      // Capacity cannot be less than MAX_INLINE, but if
      // the capacity is exactly 1, then we need to make
      // sure that we don't round down to zero growth.
      if (new_cap == cap) new_cap++;
    }
    EnsureAlloc(new_cap);
    cap = new_cap;
  }

  DCHECK(cur < cap);
  if constexpr (VERBOSE)
    Print("Write {} to idx {}\n", t, cur);

  // data()[cur] = t, but avoid using the assignment
  // operator in the case that T is not technically
  // trivially-assignable.
  std::construct_at(data() + cur, t);

  // As a small optimization, we don't unpack and repack
  // the size field, assuming that we can never increment
  // our way to overflowing into the high bit.
  SizeField()++;
}

template<class T>
T *InlineVector<T>::data() {
  if (HasAlloc()) {
    return u.ar.alloc;
  } else {
    return u.ir.buf.data();
  }
}

template<class T>
const T *InlineVector<T>::data() const {
  if (HasAlloc()) {
    return u.ar.alloc;
  } else {
    return u.ir.buf.data();
  }
}


#endif
