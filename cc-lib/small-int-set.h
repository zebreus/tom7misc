
#ifndef _CC_LIB_SMALL_INT_SET_H
#define _CC_LIB_SMALL_INT_SET_H

#include <bit>
#include <compare>
#include <cstdint>
#include <initializer_list>
#include <iterator>

#include "base/logging.h"
#include "base/macros.h"

// Holds values in 0..RADIX-1. Supports a radix up to 64.
// If you need more, consider ByteSet or std::vector<bool>!
template<int RADIX>
struct SmallIntSet {
  static_assert(RADIX > 0 && RADIX <= 64);

  // Default empty.
  SmallIntSet() : bits(0) {}
  // Value semantics (one machine word).
  SmallIntSet(const SmallIntSet &other) = default;
  SmallIntSet(SmallIntSet &&other) = default;
  SmallIntSet &operator=(const SmallIntSet &other) = default;
  SmallIntSet &operator=(SmallIntSet &&other) = default;

  explicit SmallIntSet(const std::initializer_list<int> &elts) : bits(0) {
    for (int x : elts) {
      DCHECK(x >= 0 && x < RADIX);
      bits |= (uint64_t{1} << x);
    }
  }

  // All elements.
  static SmallIntSet Top() {
    if constexpr (RADIX == 64) {
      // 1 << 64 is still undefined behavior!
      return ~uint64_t{0};
    } else {
      return SmallIntSet((uint64_t{1} << RADIX) - 1);
    }
  }

  // Empty.
  static SmallIntSet Bot() {
    return SmallIntSet(0);
  }

  bool Empty() const { return !bits; }

  bool Contains(int d) const {
    return !!(bits & (uint64_t{1} << d));
  }

  void Add(int d) {
    bits |= (uint64_t{1} << d);
  }

  void Remove(int d) {
    bits &= ~(uint64_t{1} << d);
  }

  void Toggle(int d) {
    bits ^= (uint64_t{1} << d);
  }

  int Size() const {
    return std::popcount<uint64_t>(bits);
  }

  void Clear() {
    bits = 0;
  }

  // Takes time proportional to N.
  inline int operator[](int idx) const;

  static SmallIntSet Intersection(const SmallIntSet &a,
                                    const SmallIntSet &b) {
    return SmallIntSet(a.bits & b.bits);
  }

  static SmallIntSet Union(const SmallIntSet &a,
                             const SmallIntSet &b) {
    return SmallIntSet(a.bits | b.bits);
  }

  // true if a <= b.
  static bool Subset(const SmallIntSet &a, const SmallIntSet &b) {
    return a == Intersection(a, b);
  }

  // Weirdly, we need both operator== and operator<=>.
  bool operator ==(const SmallIntSet &other) const {
    return bits == other.bits;
  }

  std::strong_ordering operator <=>(const SmallIntSet &other) const {
    return bits <=> other.bits;
  }

  // Iterate over the contents in ascending order.
  class const_iterator {
   public:
    using value_type = uint8_t;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    const_iterator(const SmallIntSet *is, int idx_in) : is(is), idx(idx_in) {
      idx = NextFrom(idx);
    }

    uint8_t operator*() const {
      // Invalid to dereference the end pointer.
      ASSUME(idx >= 0 && idx < RADIX);
      return (uint8_t)idx;
    }
    const_iterator& operator++() {
      // Invalid to increment past the end.
      ASSUME(idx >= 0 && idx < RADIX);
      idx = NextFrom(idx + 1);
      return *this;
    }
    bool operator==(const const_iterator& other) const {
      return idx == other.idx;
    }
    bool operator!=(const const_iterator& other) const {
      return idx != other.idx;
    }

   private:
    // Find the next iterator position at idx or later:
    // While idx is not end, and idx is not in the set,
    // increment it.
    int NextFrom(int idx) {
      ASSUME(idx >= 0 && idx <= RADIX);
      while (idx != RADIX) {
        // Mask zeroes for bits we've already passed; we can use
        // countr_zero to quickly find the first set bit, but
        // we don't want to count ones behind the iterator.
        uint64_t masked_word = is->bits & (~uint64_t{0} << idx);
        if (masked_word == 0) {
          // No bits left.
          return RADIX;
        } else {
          // Then we have another bit here.
          int one_bit = std::countr_zero<uint64_t>(masked_word);
          return one_bit;
        }
      }
      return idx;
    }

    // Note: This approach is from ByteSet, but we could maybe do
    // without the indirection (i.e. copy the set), since it's just
    // one word here?
    const SmallIntSet *is = nullptr;
    // 0-RADIX, where RADIX represents the end iterator.
    // When not RADIX, it is an integer in the set.
    int idx = 0;
  };

  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, RADIX); }


 private:
  SmallIntSet(uint64_t b) : bits(b) {}
  uint64_t bits = 0;
};


// Template implementations follow.

template<int RADIX>
int SmallIntSet<RADIX>::operator[](int idx_in) const {
  int idx = idx_in;
  uint64_t shift = bits;
  int dim = 0;
  for (;;) {
    DCHECK(shift != 0) << "index " << idx_in <<
      " out of bounds (radix " << RADIX << ")";
    if (!(shift & 1)) {
      int skip = std::countr_zero<uint64_t>(shift);
      dim += skip;
      shift >>= skip;
    }

    DCHECK(shift & 1);
    if (idx == 0) {
      return dim;
    }
    idx--;
    shift >>= 1;
    dim++;
  }
}


#endif
