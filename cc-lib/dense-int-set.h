
#ifndef _CC_LIB_DENSE_INT_SET_H
#define _CC_LIB_DENSE_INT_SET_H

#include <algorithm>
#include <bit>
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "base/logging.h"
#include "base/macros.h"

// Stores ints in 0...radix-1, which radix is fixed and must be known
// at construction time. If you have a static radix, consider
// SmallIntSet (or ByteSet, or std::bitset).
struct DenseIntSet {
  DenseIntSet(size_t radix) : radix(radix) {
    size_t bytes = NumBytes();
    vec = (uint64_t*)malloc(bytes);
    memset(vec, 0, bytes);
  }
  ~DenseIntSet() {
    if (vec != nullptr) {
      free(vec);
    }
    vec = nullptr;
  }

  size_t Radix() const { return radix; }

  // Value semantics. Copies.
  DenseIntSet(const DenseIntSet &other) {
    radix = other.radix;
    size_t bytes = NumWords(radix) * sizeof (uint64_t);
    vec = (uint64_t*)malloc(bytes);
    memcpy(vec, other.vec, bytes);
  }
  DenseIntSet(DenseIntSet &&other) {
    radix = other.radix;
    vec = other.vec;
    other.vec = nullptr;
  }
  DenseIntSet &operator=(const DenseIntSet &other) {
    if (this == &other) return *this;
    radix = other.radix;
    const size_t bytes = NumBytes();
    free(vec);
    vec = (uint64_t*)malloc(bytes);
    memcpy(vec, other.vec, bytes);
    return *this;
  }
  DenseIntSet &operator=(DenseIntSet &&other) {
    if (this == &other) return *this;
    radix = other.radix;
    free(vec);
    vec = other.vec;
    other.vec = 0;
    return *this;
  }

  size_t WordFor(size_t x) const {
    return x / 64;
  }

  size_t BitFor(size_t x) const {
    return x % 64;
  }

  bool Empty() const {
    return Size() == 0;
  }

  bool Contains(size_t x) const {
    DCHECK(x < radix);
    uint64_t w = vec[WordFor(x)];
    return !!(w & (uint64_t{1} << BitFor(x)));
  }

  void Add(size_t x) {
    DCHECK(x < radix);
    uint64_t &w = vec[WordFor(x)];
    w |= (uint64_t{1} << BitFor(x));
  }

  void Remove(size_t x) {
    DCHECK(x < radix);
    uint64_t &w = vec[WordFor(x)];
    w &= ~(uint64_t{1} << BitFor(x));
  }

  void Toggle(size_t x) {
    DCHECK(x < radix);
    uint64_t &w = vec[WordFor(x)];
    w ^= (uint64_t{1} << BitFor(x));
  }

  static DenseIntSet Top(size_t radix) {
    // PERF: We can set a word at a time.
    DenseIntSet ret(radix);
    for (size_t x = 0; x < radix; x++) {
      ret.Add(x);
    }
    return ret;
  }

  // PERF maybe just keep track of this?
  size_t Size() const {
    size_t res = 0;
    const size_t limit = NumWords(radix);
    for (size_t i = 0; i < limit; i++) {
      res += std::popcount<uint64_t>(vec[i]);
    }
    return res;
  }

  void Clear() {
    memset(vec, 0, NumBytes());
  }

  // Linear time. It just uses the iterators.
  inline size_t operator[](size_t idx) const;

  // When combining two sets, the radix is always the
  // larger of the two. (Typically you would use this
  // with sets that have the same radix, though.)
  static DenseIntSet Intersection(const DenseIntSet &a,
                                  const DenseIntSet &b) {
    DenseIntSet ret(std::max(a.radix, b.radix));

    // But we don't need to and with zeroes.
    size_t limit = NumWords(std::min(a.radix, b.radix));
    for (size_t i = 0; i < limit; i++) {
      ret.vec[i] = a.vec[i] & b.vec[i];
    }
    return ret;
  }

  static DenseIntSet Union(const DenseIntSet &one,
                           const DenseIntSet &two) {
    const DenseIntSet *a = &one;
    const DenseIntSet *b = &two;

    // Smaller radix first.
    if (a->radix > b->radix)
      std::swap(a, b);

    DenseIntSet ret(std::max(a->radix, b->radix));

    size_t limit_shared = NumWords(a->radix);
    size_t limit_over = NumWords(b->radix);
    size_t i = 0;
    for (; i < limit_shared; i++) {
      ret.vec[i] = a->vec[i] | b->vec[i];
    }
    for (; i < limit_over; i++) {
      ret.vec[i] = b->vec[i];
    }

    return ret;
  }

  // Two sets must have the same radix to be considered equal.
  bool operator ==(const DenseIntSet &other) const {
    if (radix != other.radix) return false;
    return 0 == std::memcmp(vec, other.vec, NumBytes());
  }

  // An arbitrary consistent total order.
  std::strong_ordering operator <=>(const DenseIntSet &other) const {
    if (radix != other.radix) return radix <=> other.radix;
    return std::memcmp(vec, other.vec, NumBytes()) <=> 0;
  }

  // true if a <= b.
  static bool Subset(const DenseIntSet &a, const DenseIntSet &b) {
    // a could have a larger radix than b, but still be a subset.
    size_t words_a = NumWords(a.radix);
    size_t words_b = NumWords(b.radix);
    size_t limit_shared = std::min(words_a, words_b);

    size_t idx = 0;
    // The prefix where they both have data.
    for (; idx < limit_shared; idx++)
      if (a.vec[idx] != (a.vec[idx] & b.vec[idx]))
        return false;

    // If a has the larger radix, then those words
    // must all be zero because they can't exist in b.
    for (; idx < words_a; idx++)
      if (a.vec[idx] != 0)
        return false;

    return true;
  }

  // Iterate over the contents in ascending order.
  class const_iterator {
   public:
    using value_type = size_t;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    size_t operator*() const {
      // Invalid to dereference the end pointer.
      DCHECK(idx < is->radix);
      ASSUME(idx < is->radix);
      return idx;
    }
    const_iterator& operator++() {
      // Invalid to increment past the end.
      DCHECK(idx < is->radix);
      ASSUME(idx < is->radix);
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
    friend struct DenseIntSet;
    const_iterator(const DenseIntSet *is, size_t idx_in) :
      is(is), idx(idx_in) {
      idx = NextFrom(idx);
    }

    // Explicitly construct the end iterator, avoiding the
    // call to NextFrom.
    enum class end_tag_t { V };
    static inline constexpr end_tag_t end_tag = end_tag_t::V;
    const_iterator(const DenseIntSet *is, end_tag_t) :
      is(is), idx(is->radix) {
    }

    // Find the next iterator position at idx or later:
    // While idx is not end, and idx is not in the set,
    // increment it.
    size_t NextFrom(size_t idx) const {
      DCHECK(idx >= 0 && idx <= is->radix);
      ASSUME(idx >= 0 && idx <= is->radix);
      if (idx != is->radix) {
        size_t widx = is->WordFor(idx);
        uint64_t word = is->vec[widx];
        int bidx = is->BitFor(idx);
        for (;;) {
          // Mask zeroes for bits we've already passed; we can use
          // countr_zero to quickly find the first set bit, but
          // we don't want to count ones behind the iterator.
          uint64_t masked_word = word & (~uint64_t{0} << bidx);
          if (masked_word == 0) {
            // No bits left in this word. Start with the beginning
            // of the next word, unless this was the last one.
            widx++;
            if (widx * 64 >= is->radix) return is->radix;
            // PERF: Might want to specialize this state since
            // we know bidx will be zero until we return. We're
            // basically just searching for a nonzero word.
            bidx = 0;
            word = is->vec[widx];
          } else {
            // If nonzero, then our next one bit is in this word.
            int one_bit = std::countr_zero<uint64_t>(masked_word);
            return widx * 64 + one_bit;
          }
        }
      }
      return idx;
    }

    // The set we're iterating over.
    const DenseIntSet *is = nullptr;
    // In [0, radix], where radix represents the end iterator.
    // When not radix, it is an integer in the set.
    size_t idx = 0;
  };

  const_iterator begin() const {
    return const_iterator(this, 0);
  }
  const_iterator end() const {
    return const_iterator(this, const_iterator::end_tag);
  }

 private:
  static constexpr int NumWords(size_t radix) {
    return (radix / 64) + ((radix % 64 > 0) ? 1 : 0);
  }

  size_t NumBytes() const {
    return NumWords(radix) * sizeof (uint64_t);
  }

  // PERF: Might be nice to have a "small vector optimization" here?
  uint64_t *vec = nullptr;
  size_t radix = 0;
};


inline size_t DenseIntSet::operator[](size_t idx) const {
  // PERF: This probably has to be linear time, but we can
  // unroll to process a word at a time. This should be much
  // faster unless the compiler can do it.

  auto it = begin();
  for (size_t i = 0; i < idx; i++) {
    DCHECK(it != end());
    ++it;
  }
  return *it;
}

#endif
