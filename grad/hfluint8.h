
#ifndef _GRAD_HFLUINT8_H
#define _GRAD_HFLUINT8_H

#include <cstdint>
#include <bit>

#include "half.h"
#include "expression.h"

// If set, use uint8 to implement it (for debugging)
#define HFLUINT8_WRAP 0

#if HFLUINT8_WRAP

struct hfluint8 {
  uint8_t b = 0;
  explicit constexpr hfluint8(uint8_t b) : b(b) {}
  uint8_t ToInt() const { return b; }

  constexpr hfluint8() : b(0) {}
  hfluint8(hfluint8 &&other) = default;
  hfluint8(const hfluint8 &other) = default;
  constexpr hfluint8 &operator =(const hfluint8 &) = default;

  static hfluint8 IsZero(hfluint8 a) {
    Cheat();
    return hfluint8(a.b ? 0x00 : 0x01);
  }
  static hfluint8 IsntZero(hfluint8 a) {
    Cheat();
    return hfluint8(a.b ? 0x01 : 0x00);
  }

  static hfluint8 Plus(hfluint8 a, hfluint8 b) {
    Cheat();
    return hfluint8(a.ToInt() + b.ToInt());
  }
  static hfluint8 Minus(hfluint8 a, hfluint8 b) {
    Cheat();
    return hfluint8(a.ToInt() - b.ToInt());
  }

  static hfluint8 BitwiseXor(hfluint8 a, hfluint8 b) {
    Cheat();
    return hfluint8(a.ToInt() ^ b.ToInt());
  }

  static hfluint8 BitwiseAnd(hfluint8 a, hfluint8 b) {
    Cheat();
    return hfluint8(a.ToInt() & b.ToInt());
  }

  static hfluint8 BitwiseOr(hfluint8 a, hfluint8 b) {
    Cheat();
    return hfluint8(a.ToInt() | b.ToInt());
  }

  template<uint8_t B>
  static hfluint8 AndWith(hfluint8 a) {
    return BitwiseAnd(a, hfluint8(B));
  }

  template<uint8_t B>
  static hfluint8 OrWith(hfluint8 a) {
    return BitwiseOr(a, hfluint8(B));
  }

  template<uint8_t B>
  static hfluint8 XorWith(hfluint8 a) {
    return BitwiseXor(a, hfluint8(B));
  }

  // Left shift by a compile-time constant.
  template<size_t n>
  static hfluint8 LeftShift(hfluint8 x) {
    return hfluint8(x.ToInt() << n);
  }

  template<size_t n>
  static hfluint8 RightShift(hfluint8 x) {
    return hfluint8(x.ToInt() >> n);
  }

  // One bit; no sign extension.
  static hfluint8 RightShift1(hfluint8 x) {
    return RightShift<1>(x);
  }

  static hfluint8 LeftShift1Under128(hfluint8 x) {
    return LeftShift<1>(x);
  }

  static hfluint8 PlusNoOverflow(hfluint8 a, hfluint8 b) {
    return Plus(a, b);
  }

  static std::pair<hfluint8, hfluint8> AddWithCarry(hfluint8 a, hfluint8 b) {
    uint32_t aa = a.b, bb = b.b;
    uint32_t cc = aa + bb;
    Cheat();
    return make_pair(hfluint8((cc & 0x100) ? 0x01 : 0x00),
                     hfluint8(cc & 0xFF));
  }

  static std::pair<hfluint8, hfluint8> SubtractWithCarry(hfluint8 a, hfluint8 b) {
    uint32_t aa = a.b, bb = b.b;
    uint32_t cc = aa - bb;
    Cheat();
    return make_pair(hfluint8((cc & 0x100) ? 0x01 : 0x00),
                     hfluint8(cc & 0xFF));
  }

  // For testing.
  uint16_t Representation() const { return b; }

  // During development; returns the number of instructions issued
  // that are not yet implemented with floats!
  static void Cheat() { num_cheats++; }
  static int64_t NumCheats() { return num_cheats; }
  static void ClearCheats() { num_cheats = 0; }

 private:

  static int64_t num_cheats;
};

#else

struct hfluint8 {
  using half = half_float::half;

  // Converting in/out uses non-linear operations, of course.
  explicit constexpr hfluint8(uint8_t b) {
    h = GetHalf(TABLE[b]);
  }

  uint8_t ToInt() const;

  constexpr hfluint8() : h(GetHalf(TABLE[0])) {}
  hfluint8(hfluint8 &&other) = default;
  hfluint8(const hfluint8 &other) = default;
  constexpr hfluint8 &operator =(const hfluint8 &) = default;

  static hfluint8 Plus(hfluint8 a, hfluint8 b);
  static hfluint8 Minus(hfluint8 a, hfluint8 b);

  // The first element is the carry; always 0 or 1.
  static std::pair<hfluint8, hfluint8> AddWithCarry(hfluint8 a, hfluint8 b);
  static std::pair<hfluint8, hfluint8> SubtractWithCarry(hfluint8 a, hfluint8 b);

  // Note: If one argument is a compile-time constant, AndWith (etc.)
  // below can be a lot faster.
  static hfluint8 BitwiseXor(hfluint8 a, hfluint8 b);
  static hfluint8 BitwiseAnd(hfluint8 a, hfluint8 b);
  static hfluint8 BitwiseOr(hfluint8 a, hfluint8 b);

  // a.k.a. !, this returns (a == 0) ? 1 : 0.
  static hfluint8 IsZero(hfluint8 a);
  // Same as !!
  static hfluint8 IsntZero(hfluint8 a);

  // (a == b) ? 1 : 0
  static hfluint8 Eq(hfluint8 a, hfluint8 b);

  // For cc = 0x01 or 0x00 (only), returns c ? t : 0.
  static hfluint8 If(hfluint8 cc, hfluint8 t);

  // For cc = 0x01 or 0x00 (only), returns c ? t : f.
  static hfluint8 IfElse(hfluint8 cc, hfluint8 t, hfluint8 f);

  // For a and b = 0x01 or 0x00 (only), returns a && b.
  static hfluint8 BooleanAnd(hfluint8 a, hfluint8 b);
  // For a and b = 0x01 or 0x00 (only), returns a || b.
  static hfluint8 BooleanOr(hfluint8 a, hfluint8 b);

  // With a compile-time constant, which is very common, and
  // can be done much faster.
  template<uint8_t b>
  static hfluint8 AndWith(hfluint8 a);

  template<uint8_t b>
  static hfluint8 OrWith(hfluint8 a);

  template<uint8_t b>
  static hfluint8 XorWith(hfluint8 a);

  // TODO: (x & c) ^ c is a common pattern in x6502,
  // which we could do in one step.

  // Left shift by a compile-time constant.
  template<size_t n>
  static hfluint8 LeftShift(hfluint8 x);

  template<size_t n>
  static hfluint8 RightShift(hfluint8 x);

  // One bit; no sign extension.
  static hfluint8 RightShift1(hfluint8 x);

  // For testing.
  uint16_t Representation() const;

  // During development; returns the number of instructions issued
  // that are not yet implemented with floats!
  static void Cheat() { num_cheats++; }
  static int64_t NumCheats() { return num_cheats; }
  static void ClearCheats() { num_cheats = 0; }

  // For benchmarking; load lazy-loaded expressions.
  // XXX no longer needed
  static void Warm();

  // Operations with preconditions. The result is undefined
  // if the preconditions are not satisfied!

  // Computes x << 1, assuming x < 128.
  static hfluint8 LeftShift1Under128(hfluint8 x) {
    return hfluint8(x.h + x.h);
  }

  // Computes a + b, as long as the sum is < 256.
  static hfluint8 PlusNoOverflow(hfluint8 a, hfluint8 b) {
    return hfluint8(a.h + b.h);
  }

 private:
  explicit hfluint8(half_float::half h) : h(h) {}
  half_float::half h;

  // Compute bitwise AND.
  static half BitwiseAndHalf(hfluint8 a, hfluint8 b);

  static half Canonicalize(half h);

  static constexpr inline uint16_t GetU16(half_float::half h) {
    return std::bit_cast<uint16_t, half_float::half>(h);
  }

  static constexpr inline half_float::half GetHalf(uint16_t u) {
    return std::bit_cast<half_float::half, uint16_t>(u);
  }

  static half RightShiftHalf1(half xh) {
    // Hand-made version. We divide, which gives us almost the right
    // answer, and then round to an integer by shifting up by 1024
    // (where only integers are representable in half precision) and
    // back. The -0.25 is just a fudge factor so that we round in the
    // right direction.
    // return hfluint8((x.h * 0.5_h - 0.25_h) + 1024.0_h - 1024.0_h);

    // Same idea, but found through search; it just happens to work.
    // Here the fudge is built into the scale.
    // approximately 0.49853515625
    static constexpr half SCALE = GetHalf(0x37fa);
    // 1741
    static constexpr half OFFSET = GetHalf(0x66cd);
    return xh * SCALE + OFFSET - OFFSET;
  }

  static half RightShiftHalf2(half xh) {
    static constexpr half SCALE = GetHalf(0x3400);
    static constexpr half OFFSET1 = GetHalf(0xb54e);
    static constexpr half OFFSET2 = GetHalf(0x6417);
    return xh * SCALE + OFFSET1 + OFFSET2 - OFFSET2;
  }

  static half RightShiftHalf3(half xh) {
    static constexpr half SCALE = GetHalf(0x3000); // 0.125
    static constexpr half OFFSET1 = GetHalf(0xb642);
    static constexpr half OFFSET2 = GetHalf(0x67bc);
    return xh * SCALE + OFFSET1 + OFFSET2 - OFFSET2;
  }

  static half RightShiftHalf7(half xh) {
    static constexpr half SCALE = GetHalf(0x1c03); // 0.0039177...
    static constexpr half OFFSET = GetHalf(0x66c8);
    return xh * SCALE + OFFSET - OFFSET;
  }

  static half RightShiftHalf4(half xh) {
    static constexpr half SCALE = GetHalf(0x2c00);  // 1/16
    static constexpr half OFFSET1 = GetHalf(0x37b5);
    static constexpr half OFFSET2 = GetHalf(0x6630);
    return (xh * SCALE - OFFSET1) + OFFSET2 - OFFSET2;
  }

  // Works for integers in [0, 512); always results in 1 or 0.
  static half RightShiftHalf8(half xh) {
    static constexpr half SCALE = GetHalf(0x1c00); // 1/256
    static constexpr half OFFSET1 = GetHalf(0xb7f6);
    static constexpr half OFFSET2 = GetHalf(0x66b0);
    return xh * SCALE + OFFSET1 + OFFSET2 - OFFSET2;
  }

  // For any input, outputs a value with mask 0x0F,
  // which preserves zeroness.
  static inline half CompressHalfFF(half xh) {
    static constexpr half a = GetHalf(0x4b80);  // 15
    static constexpr half b = GetHalf(0x7bf7);  // 65248
    static constexpr half c = GetHalf(0x2800);  // 0.03125
    return (xh + a + b - b) * c;
  }

  // For input with mask 0x0F, outputs 0x01 or 0x00, preserving
  // zeroness.
  static inline half CompressHalf0F(half xh) {
    static constexpr half a = GetHalf(0x477a);  // 7.4765...
    static constexpr half b = GetHalf(0x741f);  // 16880
    static constexpr half c = GetHalf(0x2c00);  // 0.0625
    return (xh + a + b - b) * c;
  }

  // We want a constexpr constructor, so this is the representation
  // of each of the 256 bytes.
  static constexpr uint16_t TABLE[256] = {
    0x0000, 0x3c00, 0x4000, 0x4200, 0x4400, 0x4500, 0x4600, 0x4700,
    0x4800, 0x4880, 0x4900, 0x4980, 0x4a00, 0x4a80, 0x4b00, 0x4b80,
    0x4c00, 0x4c40, 0x4c80, 0x4cc0, 0x4d00, 0x4d40, 0x4d80, 0x4dc0,
    0x4e00, 0x4e40, 0x4e80, 0x4ec0, 0x4f00, 0x4f40, 0x4f80, 0x4fc0,
    0x5000, 0x5020, 0x5040, 0x5060, 0x5080, 0x50a0, 0x50c0, 0x50e0,
    0x5100, 0x5120, 0x5140, 0x5160, 0x5180, 0x51a0, 0x51c0, 0x51e0,
    0x5200, 0x5220, 0x5240, 0x5260, 0x5280, 0x52a0, 0x52c0, 0x52e0,
    0x5300, 0x5320, 0x5340, 0x5360, 0x5380, 0x53a0, 0x53c0, 0x53e0,
    0x5400, 0x5410, 0x5420, 0x5430, 0x5440, 0x5450, 0x5460, 0x5470,
    0x5480, 0x5490, 0x54a0, 0x54b0, 0x54c0, 0x54d0, 0x54e0, 0x54f0,
    0x5500, 0x5510, 0x5520, 0x5530, 0x5540, 0x5550, 0x5560, 0x5570,
    0x5580, 0x5590, 0x55a0, 0x55b0, 0x55c0, 0x55d0, 0x55e0, 0x55f0,
    0x5600, 0x5610, 0x5620, 0x5630, 0x5640, 0x5650, 0x5660, 0x5670,
    0x5680, 0x5690, 0x56a0, 0x56b0, 0x56c0, 0x56d0, 0x56e0, 0x56f0,
    0x5700, 0x5710, 0x5720, 0x5730, 0x5740, 0x5750, 0x5760, 0x5770,
    0x5780, 0x5790, 0x57a0, 0x57b0, 0x57c0, 0x57d0, 0x57e0, 0x57f0,
    0x5800, 0x5808, 0x5810, 0x5818, 0x5820, 0x5828, 0x5830, 0x5838,
    0x5840, 0x5848, 0x5850, 0x5858, 0x5860, 0x5868, 0x5870, 0x5878,
    0x5880, 0x5888, 0x5890, 0x5898, 0x58a0, 0x58a8, 0x58b0, 0x58b8,
    0x58c0, 0x58c8, 0x58d0, 0x58d8, 0x58e0, 0x58e8, 0x58f0, 0x58f8,
    0x5900, 0x5908, 0x5910, 0x5918, 0x5920, 0x5928, 0x5930, 0x5938,
    0x5940, 0x5948, 0x5950, 0x5958, 0x5960, 0x5968, 0x5970, 0x5978,
    0x5980, 0x5988, 0x5990, 0x5998, 0x59a0, 0x59a8, 0x59b0, 0x59b8,
    0x59c0, 0x59c8, 0x59d0, 0x59d8, 0x59e0, 0x59e8, 0x59f0, 0x59f8,
    0x5a00, 0x5a08, 0x5a10, 0x5a18, 0x5a20, 0x5a28, 0x5a30, 0x5a38,
    0x5a40, 0x5a48, 0x5a50, 0x5a58, 0x5a60, 0x5a68, 0x5a70, 0x5a78,
    0x5a80, 0x5a88, 0x5a90, 0x5a98, 0x5aa0, 0x5aa8, 0x5ab0, 0x5ab8,
    0x5ac0, 0x5ac8, 0x5ad0, 0x5ad8, 0x5ae0, 0x5ae8, 0x5af0, 0x5af8,
    0x5b00, 0x5b08, 0x5b10, 0x5b18, 0x5b20, 0x5b28, 0x5b30, 0x5b38,
    0x5b40, 0x5b48, 0x5b50, 0x5b58, 0x5b60, 0x5b68, 0x5b70, 0x5b78,
    0x5b80, 0x5b88, 0x5b90, 0x5b98, 0x5ba0, 0x5ba8, 0x5bb0, 0x5bb8,
    0x5bc0, 0x5bc8, 0x5bd0, 0x5bd8, 0x5be0, 0x5be8, 0x5bf0, 0x5bf8,
  };

  static int64_t num_cheats;
};

#endif

// Overloaded operators.

inline hfluint8 operator +(const hfluint8 &a, const hfluint8 &b) {
  return hfluint8::Plus(a, b);
}

inline hfluint8 operator -(const hfluint8 &a, const hfluint8 &b) {
  return hfluint8::Minus(a, b);
}

inline hfluint8 operator ^(const hfluint8 &a, const hfluint8 &b) {
  return hfluint8::BitwiseXor(a, b);
}

inline hfluint8 operator |(const hfluint8 &a, const hfluint8 &b) {
  return hfluint8::BitwiseOr(a, b);
}

inline hfluint8 operator &(const hfluint8 &a, const hfluint8 &b) {
  return hfluint8::BitwiseAnd(a, b);
}

inline hfluint8 operator +(const hfluint8 &a) {
  return a;
}

inline hfluint8 operator -(const hfluint8 &a) {
  return hfluint8::Minus(hfluint8(0), a);
}

inline hfluint8 operator ~(const hfluint8 &a) {
  return hfluint8::XorWith<0xFF>(a);
}

inline hfluint8& operator++(hfluint8 &a) {
  a = hfluint8::Plus(a, hfluint8(0x01));
  return a;
}

inline hfluint8 operator++(hfluint8 &a, int) {
  hfluint8 ret = a;
  a = hfluint8::Plus(a, hfluint8(0x01));
  return ret;
}

inline hfluint8& operator--(hfluint8 &a) {
  a = hfluint8::Minus(a, hfluint8(0x01));
  return a;
}

inline hfluint8 operator--(hfluint8 &a, int) {
  hfluint8 ret = a;
  a = hfluint8::Minus(a, hfluint8(0x01));
  return ret;
}

inline hfluint8& operator+=(hfluint8 &a, const hfluint8 &b) {
  a = a + b;
  return a;
}

inline hfluint8& operator-=(hfluint8 &a, const hfluint8 &b) {
  a = a - b;
  return a;
}

inline hfluint8& operator^=(hfluint8 &a, const hfluint8 &b) {
  a = a ^ b;
  return a;
}

inline hfluint8& operator|=(hfluint8 &a, const hfluint8 &b) {
  a = a | b;
  return a;
}

inline hfluint8& operator&=(hfluint8 &a, const hfluint8 &b) {
  a = a & b;
  return a;
}


// Template implementations.
#if !HFLUINT8_WRAP

template<size_t N>
hfluint8 hfluint8::LeftShift(hfluint8 x) {
  if constexpr (N == 0) {
    return x;
  } else {
    hfluint8 y = LeftShift<N - 1>(x);
    return y + y;
  }
}

template<size_t N>
hfluint8 hfluint8::RightShift(hfluint8 x) {
  if constexpr (N == 0) {
    return x;
  } else if constexpr (N >= 7) {
    hfluint8 y = RightShift<N - 7>(x);
    return hfluint8(RightShiftHalf7(y.h));
  } else if constexpr (N >= 4) {
    hfluint8 y = RightShift<N - 4>(x);
    return hfluint8(RightShiftHalf4(y.h));
  } else if constexpr (N >= 3) {
    hfluint8 y = RightShift<N - 3>(x);
    return hfluint8(RightShiftHalf3(y.h));
  } else if constexpr (N >= 2) {
    hfluint8 y = RightShift<N - 2>(x);
    return hfluint8(RightShiftHalf2(y.h));
  } else {
    hfluint8 y = RightShift<N - 1>(x);
    return RightShift1(y);
  }
}

template<uint8_t B>
hfluint8 hfluint8::AndWith(hfluint8 a) {
  // As in BitwiseAndHalf, we compute the result directly in [0, 255]
  // space.

  // XXX unroll this so that it's clear that it's compile-time
  half common_bits = GetHalf(0x0000);
  hfluint8 aa = a;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    // Low order bit as a - ((a >> 1) << 1)
    hfluint8 aashift = RightShift1(aa);

    if ((1 << bit_idx) & B) {
      // The shifted value does not have its high bit fast,
      // so we have a fast left shift (x + x).
      half bit = aa.h - LeftShift1Under128(aashift).h;
      // We know the bit from the constant b is 1, so copy the
      // bit from a.
      // Computes 2^bit_idx
      const half scale = GetHalf(0x3c00 + 0x400 * bit_idx);
      common_bits += scale * bit;
    } else {
      // Otherwise it is zero and contributes nothing to the output.
    }
    // and keep shifting down
    // PERF: We know we can do larger shifts faster, so we may
    // be able to do some template tricks that would help when
    // the constant is sparse.
    aa = aashift;
  }

  return hfluint8(common_bits);
}

template<uint8_t B>
hfluint8 hfluint8::OrWith(hfluint8 a) {
  half result = GetHalf(0x0000);
  hfluint8 aa = a;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    // Low order bit as a - ((a >> 1) << 1)
    hfluint8 aashift = RightShift1(aa);

    // Computes 2^bit_idx
    const half scale = GetHalf(0x3c00 + 0x400 * bit_idx);
    if ((1 << bit_idx) & B) {
      // If the bit is one in the source, always emit one.
      result += scale;
    } else {
      // Else copy the bit from a.
      half bit = aa.h - LeftShift1Under128(aashift).h;
      result += scale * bit;
    }
    // and keep shifting down
    aa = aashift;
  }

  return hfluint8(result);
}

template<uint8_t B>
hfluint8 hfluint8::XorWith(hfluint8 a) {
  static constexpr half HALF1 = GetHalf(0x3c00);
  half result = GetHalf(0x0000);
  hfluint8 aa = a;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    // Low order bit as a - ((a >> 1) << 1)
    hfluint8 aashift = RightShift1(aa);
    half bit = aa.h - LeftShift1Under128(aashift).h;
    const half scale = GetHalf(0x3c00 + 0x400 * bit_idx);
    if ((1 << bit_idx) & B) {
      // Toggle the bit.
      result += scale * (HALF1 - bit);
    } else {
      // Else copy the bit from a.
      result += scale * bit;
    }
    // and keep shifting down
    aa = aashift;
  }

  return hfluint8(result);
}

#endif

#endif


