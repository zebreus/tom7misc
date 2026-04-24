
#ifndef _CC_LIB_BIT_STRING_H
#define _CC_LIB_BIT_STRING_H

#include <string>
#include <vector>
#include <cstdint>

#include "base/logging.h"

struct BitStringView;
struct BitStringConstView;

// A vector of bits, which can be converted to bytes (0th
// bit is the most significant bit of the first byte; zero
// padding at the end).
struct BitString {
  static constexpr size_t npos = std::string::npos;

  // Create a new empty bitstring.
  BitString() { }
  inline BitString(size_t bits, bool bit = false);

  // Hint that we will want to store this many bits.
  inline void Reserve(size_t bits);

  /* Appends the n low-order bits to the bit buffer. */
  inline void WriteBits(int n, uint64_t bits);
  inline void WriteBit(bool bit);

  /* Get the full contents of the buffer as a string,
     padded with zeroes at the end if necessary. */
  inline std::string GetString() const;
  inline std::vector<uint8_t> GetBytes() const;

  inline size_t NumBits() const { return num_bits; }
  inline size_t Size() const { return num_bits; }

  // True if every bit is zero.
  inline bool Zero() const;

  /* give the number of bytes needed to store n bits */
  static inline size_t Ceil(int64_t bits) {
    return (bits >> 3) + !!(bits & 7);
  }

  inline bool Get(size_t idx) const;
  inline void Set(size_t idx, bool bit);
  inline bool operator [](size_t idx) const;

  inline BitStringView View();
  inline BitStringConstView View() const;

 private:
  friend struct BitStringView;
  friend struct BitStringConstView;
  // Always Ceil(num_bits) bytes, with trailing zero bits.
  std::vector<uint8_t> bytes;
  size_t num_bits = 0;
};

// A "pointer" into a bit string; similar to std::span but
// supporting sub-byte ranges.
struct BitStringView {
  static constexpr size_t npos = std::string::npos;

  BitStringView() = default;
  BitStringView(const BitStringView &other) = default;
  BitStringView &operator=(const BitStringView &other) = default;
  BitStringView(BitString &parent) :
    parent(&parent), offset(0), length(parent.num_bits) {}

  size_t Size() const { return length; }

  inline void Set(size_t idx, bool bit);
  inline bool Get(size_t idx) const;

  inline void RemovePrefix(size_t n);
  inline void RemoveSuffix(size_t n);

  inline BitStringView Sub(size_t offset, size_t length = npos);
  inline BitStringConstView Sub(size_t offset, size_t length = npos) const;

 private:
  friend struct BitString;
  friend struct BitStringConstView;
  BitStringView(BitString *parent, size_t offset, size_t length) :
    parent(parent), offset(offset), length(length) {}

  BitString *parent = nullptr;
  // In bits.
  size_t offset = 0;
  size_t length = 0;
};

struct BitStringConstView {
  static constexpr size_t npos = std::string::npos;

  BitStringConstView(const BitStringView &other) :
    parent(other.parent), offset(other.offset), length(other.length) {}
  BitStringConstView() = default;
  BitStringConstView(const BitStringConstView &other) = default;
  BitStringConstView &operator=(const BitStringConstView &other) = default;
  BitStringConstView(const BitString &parent) :
    parent(&parent), offset(0), length(parent.num_bits) {}

  inline size_t Size() const { return length; }

  inline bool Get(size_t idx) const;
  inline bool operator[](size_t idx) const { return Get(idx); }

  inline void RemovePrefix(size_t n);
  inline void RemoveSuffix(size_t n);

  inline BitStringConstView Sub(size_t offset, size_t length = npos) const;

 private:
  friend struct BitString;
  friend struct BitStringView;
  BitStringConstView(const BitString *parent, size_t offset, size_t length) :
    parent(parent), offset(offset), length(length) {}

  const BitString *parent = nullptr;
  size_t offset = 0;
  size_t length = 0;
};


// Inline implementations follow.

BitString::BitString(size_t bits, bool bit) {
  // PERF: Faster to create the vector with the
  // correct size in the first place.
  Reserve(bits);

  if (bit) {
    while (bits >= 8) {
      bytes.push_back(0xFF);
      num_bits += 8;
      bits -= 8;
    }
  } else {
    while (bits >= 8) {
      bytes.push_back(0x00);
      num_bits += 8;
      bits -= 8;
    }
  }

  for (; bits > 0; bits--) WriteBit(bit);
}

std::string BitString::GetString() const {
  std::string s;
  s.reserve(bytes.size());
  for (uint8_t b : bytes) s.push_back(b);
  return s;
}

void BitString::Set(size_t idx, bool bit) {
  DCHECK(idx < num_bits) << idx << " vs " << num_bits;
  if (bit) {
    bytes[idx >> 3] |= (1 << (7 - (idx & 7)));
  } else {
    bytes[idx >> 3] &= ~(1 << (7 - (idx & 7)));
  }
}

bool BitString::Zero() const {
  for (uint8_t b : bytes)
    if (b != 0)
      return false;
  return true;
}

std::vector<uint8_t> BitString::GetBytes() const {
  return bytes;
}

void BitString::WriteBit(bool bit) {
  if ((num_bits & 7) == 0) {
    bytes.push_back(0x00);
  }

  if (bit) {
    bytes[num_bits >> 3] |= (1 << (7 - (num_bits & 7)));
  }
  num_bits++;
}

void BitString::WriteBits(int n, uint64_t b) {
  for (int i = 0; i < n; i ++) {
    bool bit = !!(b & (uint64_t{1} << (n - (i + 1))));
    WriteBit(bit);
  }
}

void BitString::Reserve(size_t bits) {
  bytes.reserve(Ceil(bits));
}

bool BitString::Get(size_t idx) const {
  DCHECK(idx < num_bits);
  return !!(bytes[idx >> 3] & (1 << (7 - (idx & 7))));
}

bool BitString::operator[](size_t idx) const {
  return Get(idx);
}


BitStringView BitString::View() {
  return BitStringView(this, 0, num_bits);
}

BitStringConstView BitString::View() const {
  return BitStringConstView(this, 0, num_bits);
}

void BitStringView::RemovePrefix(size_t n) {
  DCHECK(n <= length);
  length -= n;
  offset += n;
}

void BitStringView::RemoveSuffix(size_t n) {
  DCHECK(n <= length);
  length -= n;
}

void BitStringView::Set(size_t idx, bool bit) {
  DCHECK(idx < length);
  size_t bit_idx = offset + idx;
  if (bit) {
    parent->bytes[bit_idx >> 3] |= (1 << (7 - (bit_idx & 7)));
  } else {
    parent->bytes[bit_idx >> 3] &= ~(1 << (7 - (bit_idx & 7)));
  }
}

bool BitStringView::Get(size_t idx) const {
  DCHECK(idx < length);
  size_t bit_idx = offset + idx;
  return !!(parent->bytes[bit_idx >> 3] & (1 << (7 - (bit_idx & 7))));
}

bool BitStringConstView::Get(size_t idx) const {
  DCHECK(idx < length);
  size_t bit_idx = offset + idx;
  return !!(parent->bytes[bit_idx >> 3] & (1 << (7 - (bit_idx & 7))));
}

void BitStringConstView::RemovePrefix(size_t n) {
  DCHECK(n <= length);
  length -= n;
  offset += n;
}

void BitStringConstView::RemoveSuffix(size_t n) {
  DCHECK(n <= length);
  length -= n;
}

BitStringView BitStringView::Sub(size_t off, size_t len) {
  DCHECK(off <= length);
  if (len == npos) {
    len = length - off;
  }
  DCHECK(len <= length - off);
  return BitStringView(parent, offset + off, len);
}

BitStringConstView BitStringView::Sub(size_t off, size_t len) const {
  DCHECK(off <= length);
  if (len == npos) {
    len = length - off;
  }
  DCHECK(len <= length - off);
  return BitStringConstView(parent, offset + off, len);
}

BitStringConstView BitStringConstView::Sub(size_t off, size_t len) const {
  DCHECK(off <= length);
  if (len == npos) {
    len = length - off;
  }
  DCHECK(len <= length - off);
  return BitStringConstView(parent, offset + off, len);
}

#endif
