// Various representations of a set of bytes, for program
// analysis applications.
//
// TODO: To cc-lib?

#ifndef _BYTESET_H
#define _BYTESET_H

#include <cstddef>
#include <iterator>
#include <vector>
#include <cstdint>
#include <bitset>

#include "base/logging.h"

// Each of the following is a lattice (i.e. has subset, union, and
// intersection operations) representing a set of bytes.

// Represents the set of bytes exactly (the "free lattice").
struct ByteSet {
  ByteSet() { member.reset(); }
  bool Empty() const { return member.none(); }
  bool Contains(uint8_t b) const { return member.test(b); }
  void Add(uint8_t b) { member.set(b); }
  static ByteSet Top() {
    ByteSet s;
    s.member.reset();
    s.member.flip();
    return s;
  }

  static ByteSet Bottom() {
    ByteSet s;
    s.member.reset();
    return s;
  }

  int Size() const {
    // beware: bitset<256>::size() is always 256.
    return member.count();
  }

  // Allows iterating over bytes that are set. Note that this
  // always takes 256 steps to iterate over the set, even if it
  // is sparse.
  class const_iterator {
   public:
    using value_type = uint8_t;
    using difference_type = std::ptrdiff_t;
    // using reference = uint8_t;
    using iterator_category = std::input_iterator_tag;

    const_iterator(const ByteSet *bs, int idx_in) : bs(bs), idx(idx_in) {
      // Advance to the next set bit:
      while (idx < 256 && !bs->member.test(idx)) ++idx;
    }

    uint8_t operator*() const { return (uint8_t)idx; }
    const_iterator& operator++() {
      ++idx;
      while (idx < 256 && !bs->member.test(idx)) ++idx;
      return *this;
    }
    bool operator==(const const_iterator& other) const {
      return idx == other.idx;
    }
    bool operator!=(const const_iterator& other) const {
      return idx != other.idx;
    }

   private:
    const ByteSet *bs = nullptr;
    // 0-256, where 256 represents the end iterator.
    // When 0-255, it is a byte in the set.
    int idx = 0;
  };

  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, 256); }

 private:
  friend class const_iterator;
  // TODO: I think I can make this more efficient. Iterating
  // over the bits requires testing every value, but something
  // like countl_zero should make it much faster for sparse sets.
  std::bitset<256> member;
  static_assert(sizeof(member) == 32);
};

struct ByteSet64 {
  // Deterministic. But note that there are multiple ways of
  // representing a set, and this must be lossy for cardinality
  // reasons. Always represents a superset of the input (and
  // ideally the same set).
  ByteSet64(const ByteSet &s);
  ByteSet64() : type(EMPTY) {
    for (int i = 0; i < 7; i++) payload[i] = 0;
  }

  bool Contains(uint8_t) const;
  // Not cheap because of the possibility of duplicate
  // values or overlapping ranges.
  int Size() const;

  void Add(uint8_t b);

  // The representation always fits in 8 bytes, but uses
  // different formats.
  //
  // TODO: So many possible tricks here. But since we use
  // so few types, we could consider allocating some type
  // bits to something else (like "has zero"). Another
  // thing we could do is give bitmasks (known-1 and
  // known-0).
  enum Type : uint8_t {
    EMPTY = 0,
    // 7 specific values. To store fewer than 7 values,
    // repeat the last value.
    VALUES = 1,
    // 3 ranges, of the form (start,length). Order by
    // start byte and use ranges with length=0 if there.
    // are fewer than 3. Note that the ranges can wrap
    // around!
    RANGES = 2,
  };
  uint8_t type = 0;
  uint8_t payload[7] = {};

  static bool InInterval(uint8_t start, uint8_t len, uint8_t v) {
    int end = (int)start + (int)len;
    if (v >= start && v < end) return true;
    if (v < start && v + 256 < end) return true;
    return false;
  }
};
static_assert(sizeof(ByteSet64) == 8);

#endif
