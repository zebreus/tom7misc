
#ifndef _CC_LIB_BIT_STRING_H
#define _CC_LIB_BIT_STRING_H

#include <cstring>
#include <format>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <string_view>

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
  // Set all the bits to the same value.
  inline void Clear(bool bit = false);

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

  // Comparisons via implicit conversion to const view.

  inline bool Get(size_t idx) const;
  inline void Set(size_t idx, bool bit);
  inline bool operator [](size_t idx) const;

  inline BitStringView View();
  inline BitStringConstView View() const;

  inline std::string ToASCII() const;
  static inline std::optional<BitString> FromASCII(std::string_view ascii);

 private:
  friend struct BitStringView;
  friend struct BitStringConstView;
  // Change the size, but leave the data in an unspecified (but valid)
  // state.
  inline void ResizeUninitialized(size_t bits);
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

  inline std::string ToASCII() const;

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

  inline std::string ToASCII() const;

  inline static auto Spaceship(BitStringConstView a, BitStringConstView b) {
    size_t min_len = a.length < b.length ? a.length : b.length;
    for (size_t i = 0; i < min_len; i++) {
      bool l = a[i], r = b[i];
      if (l != r) return l <=> r;
    }
    return a.length <=> b.length;
  }

  inline static bool Equal(BitStringConstView a, BitStringConstView b) {
    if (a.length != b.length) return false;
    for (size_t i = 0; i < a.length; i++) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }


 private:
  friend struct BitString;
  friend struct BitStringView;
  BitStringConstView(const BitString *parent, size_t offset, size_t length) :
    parent(parent), offset(offset), length(length) {}

  const BitString *parent = nullptr;
  size_t offset = 0;
  size_t length = 0;
};

inline auto operator<=>(BitStringConstView a, BitStringConstView b) {
  return BitStringConstView::Spaceship(a, b);
}

inline bool operator==(BitStringConstView a, BitStringConstView b) {
  return BitStringConstView::Equal(a, b);
}

// Inline implementations follow.

BitString::BitString(size_t bits, bool bit) {
  // PERF: Faster to create the vector with the
  // correct data in the first place.
  ResizeUninitialized(bits);
  Clear(bit);
}

void BitString::Clear(bool bit) {
  memset(bytes.data(), bit ? 0xFF : 0x00, bytes.size());

  // Trailing bits must be zero. We need to branch on
  // the possibility that there are no bytes anyway, so
  // we test whether we have nonzero slack.
  if (num_bits & 7) {
    bytes.back() &= (0xFF << (8 - (num_bits & 7)));
  }
}

void BitString::ResizeUninitialized(size_t bits) {
  int num_bytes = Ceil(bits);
  // (PERF: This actually does initialize, but prepping for
  // resize_and_overwrite).
  bytes.resize(num_bytes, 0);
  // To be valid, the trailing bits must be zero.
  if (num_bytes > 0) bytes.back() = 0;
  num_bits = bits;
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

inline std::string BitString::ToASCII() const {
  return View().ToASCII();
}

inline std::string BitStringView::ToASCII() const {
  return BitStringConstView(*this).ToASCII();
}

inline std::string BitStringConstView::ToASCII() const {
  std::string result = std::format("{:x}.", length);
  result.reserve(result.length() + length / 6 + (length % 6 != 0 ? 1 : 0));

  constexpr std::string_view CHARS =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  for (size_t i = 0; i < length; i += 6) {
    int val = 0;
    for (int b = 0; b < 6; b++) {
      if (i + b < length && Get(i + b)) {
        val |= (1 << (5 - b));
      }
    }
    result.push_back(CHARS[val]);
  }

  return result;
}

inline std::optional<BitString> BitString::FromASCII(std::string_view s) {
  size_t dot = s.find('.');
  if (dot == std::string_view::npos) return std::nullopt;

  std::string_view hex_len = s.substr(0, dot);
  // Reject empty length or strings that would overflow size_t
  if (hex_len.empty() || hex_len.size() > sizeof(size_t) * 2) {
    return std::nullopt;
  }

  size_t length = 0;
  for (char c : hex_len) {
    length <<= 4;
    if (c >= '0' && c <= '9') {
      length |= (c - '0');
    } else if (c >= 'a' && c <= 'f') {
      length |= (c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      length |= (c - 'A' + 10);
    } else {
      return std::nullopt;
    }
  }

  std::string_view data = s.substr(dot + 1);
  size_t expected_chars = length / 6 + (length % 6 != 0 ? 1 : 0);
  if (data.size() != expected_chars) {
    return std::nullopt;
  }

  BitString bs;
  bs.Reserve(length);

  for (char c : data) {
    int val = 0;
    if (c >= 'A' && c <= 'Z') {
      val = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
      val = c - 'a' + 26;
    } else if (c >= '0' && c <= '9') {
      val = c - '0' + 52;
    } else if (c == '-') {
      val = 62;
    } else if (c == '_') {
      val = 63;
    } else {
      return std::nullopt;
    }

    for (int b = 0; b < 6; b++) {
      if (bs.NumBits() < length) {
        bs.WriteBit(!!(val & (1 << (5 - b))));
      } else if (val & (1 << (5 - b))) {
        // Enforce canonical representation: unused bits must be 0
        return std::nullopt;
      }
    }
  }

  return bs;
}

#endif
