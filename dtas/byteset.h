// Various representations of a set of bytes, for program
// analysis applications.
//
// TODO: To cc-lib?

#ifndef _BYTESET_H
#define _BYTESET_H

#include <cstddef>
#include <iterator>
#include <cstdint>
#include <bitset>
#include <compare>

// Each of the following is a lattice (i.e. has subset, union, and
// intersection operations) representing a set of bytes.

// Represents the set of bytes exactly (the "free lattice").
struct ByteSet {
  ByteSet() { member.reset(); }
  bool Empty() const { return member.none(); }
  bool Contains(uint8_t b) const { return member.test(b); }
  void Add(uint8_t b) { member.set(b); }
  void AddSet(const ByteSet &b);

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

  static ByteSet Singleton(uint8_t b) {
    ByteSet s;
    s.Add(b);
    return s;
  }

  static ByteSet Union(const ByteSet &a, const ByteSet &b);

  int Size() const {
    // beware: bitset<256>::size() is always 256.
    return member.count();
  }

  // Get one element from the set; intended for uses where
  // the set has size 1. Aborts if the set is empty.
  uint8_t GetSingleton() const;

  template<class F>
  ByteSet Map(const F &f) {
    ByteSet ret;
    for (uint8_t v : *this) {
      ret.Add(f(v));
    }
    return ret;
  }

  void Clear() {
    member.reset();
  }

  // Allows iterating over bytes that are set. Note that this
  // internally iterates over all 256 values, even if it is sparse.
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

  // Weirdly, we need both operator== and operator<=>.
  bool operator ==(const ByteSet &other) const {
    return member == other.member;
  }

  std::strong_ordering operator <=>(const ByteSet &other) const {
    // PERF: Again, with direct access to words, we could do
    // this much faster.
    for (int i = 0; i < 256; i++) {
      bool a = member.test(i);
      bool b = other.member.test(i);
      if (a != b) {
        return a ? std::strong_ordering::greater :
          std::strong_ordering::less;
      }
    }
    return std::strong_ordering::equal;
  }

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
  explicit ByteSet64(const ByteSet &s);
  ByteSet64() : type(EMPTY) {
    for (int i = 0; i < 7; i++) payload[i] = 0;
  }

  static ByteSet64 Singleton(uint8_t b) {
    ByteSet64 s;
    s.Set(VALUES, b, b, b, b, b, b, b);
    return s;
  }

  ByteSet ToByteSet() const;

  // imperative union
  void AddSet(const ByteSet64 &other);
  void AddSet(const ByteSet &other);

  static ByteSet64 Union(const ByteSet64 &a, const ByteSet64 &b);

  // Get one element from the set; intended for uses where
  // the set has size 1. Aborts if the set is empty.
  uint8_t GetSingleton() const;

  bool Contains(uint8_t) const;
  // Not cheap because of the possibility of duplicate
  // values or overlapping ranges.
  int Size() const;

  void Clear();
  void Add(uint8_t b);

  bool operator ==(const ByteSet64 &other) const;
  std::strong_ordering operator <=>(const ByteSet64 &other) const;

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
  void Set(uint8_t typ, uint8_t p0 = 0, uint8_t p1 = 0, uint8_t p2 = 0,
           uint8_t p3 = 0, uint8_t p4 = 0, uint8_t p5 = 0, uint8_t p6 = 0) {
    type = typ;
    payload[0] = p0;
    payload[1] = p1;
    payload[2] = p2;
    payload[3] = p3;
    payload[4] = p4;
    payload[5] = p5;
    payload[6] = p6;
  }
};
static_assert(sizeof(ByteSet64) == 8);

#endif
