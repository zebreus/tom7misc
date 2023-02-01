
#ifndef _GRAD_FLUINT8_H
#define _GRAD_FLUINT8_H

#include <cstdint>
#include <bit>

#include "half.h"
#include "expression.h"

// If set, use uint8 to implement it (for debugging)
#define FLUINT8_WRAP 0

#if FLUINT8_WRAP

struct Fluint8 {
  uint8_t b = 0;
  explicit constexpr Fluint8(uint8_t b) : b(b) {}
  uint8_t ToInt() const { return b; }

  // TODO: This should not be supported; instead we want like ?:
  bool operator !() const {
    Cheat();
    return ToInt() == 0;
  }

  constexpr Fluint8() : b(0) {}
  Fluint8(Fluint8 &&other) = default;
  Fluint8(const Fluint8 &other) = default;
  constexpr Fluint8 &operator =(const Fluint8 &) = default;

  static Fluint8 Plus(Fluint8 a, Fluint8 b) {
    Cheat();
    return Fluint8(a.ToInt() + b.ToInt());
  }
  static Fluint8 Minus(Fluint8 a, Fluint8 b) {
    Cheat();
    return Fluint8(a.ToInt() - b.ToInt());
  }

  static Fluint8 BitwiseXor(Fluint8 a, Fluint8 b) {
    Cheat();
    return Fluint8(a.ToInt() ^ b.ToInt());
  }

  static Fluint8 BitwiseAnd(Fluint8 a, Fluint8 b) {
    Cheat();
    return Fluint8(a.ToInt() & b.ToInt());
  }

  static Fluint8 BitwiseOr(Fluint8 a, Fluint8 b) {
    Cheat();
    return Fluint8(a.ToInt() | b.ToInt());
  }

  template<uint8_t B>
  static Fluint8 AndWith(Fluint8 a) {
    return BitwiseAnd(a, Fluint8(B));
  }

  // Left shift by a compile-time constant.
  template<size_t n>
  static Fluint8 LeftShift(Fluint8 x) {
    return Fluint8(x.ToInt() << n);
  }

  template<size_t n>
  static Fluint8 RightShift(Fluint8 x) {
    return Fluint8(x.ToInt() >> n);
  }

  // One bit; no sign extension.
  static Fluint8 RightShift1(Fluint8 x) {
    return RightShift<1>(x);
  }

  // For testing.
  uint16_t Representation() const { return b; }

  // During development; returns the number of instructions issued
  // that are not yet implemented with floats!
  static void Cheat() { num_cheats++; }
  static int64_t NumCheats() { return num_cheats; }
  static void ClearCheats() { num_cheats = 0; }

  static void Warm() {}

 private:

  static int64_t num_cheats;
};

#else

struct Fluint8 {
  using half = half_float::half;

  // Converting in/out uses non-linear operations, of course.
  explicit constexpr Fluint8(uint8_t b) {
    h = GetHalf(TABLE[b]);
  }

  uint8_t ToInt() const;

  // TODO: This should not be supported; instead we want like ?:
  // bool operator !() const {
  // return IsZero(*this);
  // }

  constexpr Fluint8() : h(GetHalf(TABLE[0])) {}
  Fluint8(Fluint8 &&other) = default;
  Fluint8(const Fluint8 &other) = default;
  constexpr Fluint8 &operator =(const Fluint8 &) = default;

  static Fluint8 Plus(Fluint8 a, Fluint8 b);
  static Fluint8 Minus(Fluint8 a, Fluint8 b);

  // The first element is the carry; always 0 or 1.
  static std::pair<Fluint8, Fluint8> AddWithCarry(Fluint8 a, Fluint8 b);
  static std::pair<Fluint8, Fluint8> SubtractWithCarry(Fluint8 a, Fluint8 b);

  // PERF: Bitwise ops with a constant are very common in 6502,
  // and could be much faster.
  static Fluint8 BitwiseXor(Fluint8 a, Fluint8 b);
  static Fluint8 BitwiseAnd(Fluint8 a, Fluint8 b);
  static Fluint8 BitwiseOr(Fluint8 a, Fluint8 b);

  // a.k.a. !, this returns (a == 0) ? 1 : 0.
  static Fluint8 IsZero(Fluint8 a);
  // Same as !!
  static Fluint8 IsntZero(Fluint8 a);

  // With a compile-time constant, which is very common, and
  // can be done much faster.
  template<uint8_t b>
  static Fluint8 AndWith(Fluint8 a);

  template<uint8_t b>
  static Fluint8 OrWith(Fluint8 a);

  template<uint8_t b>
  static Fluint8 XorWith(Fluint8 a);

  // TODO: (x & c) ^ c is a common pattern in x6502,
  // which we could do in one step.

  // Left shift by a compile-time constant.
  template<size_t n>
  static Fluint8 LeftShift(Fluint8 x);

  template<size_t n>
  static Fluint8 RightShift(Fluint8 x);

  // One bit; no sign extension.
  static Fluint8 RightShift1(Fluint8 x);

  // For testing.
  uint16_t Representation() const;

  // During development; returns the number of instructions issued
  // that are not yet implemented with floats!
  static void Cheat() { num_cheats++; }
  static int64_t NumCheats() { return num_cheats; }
  static void ClearCheats() { num_cheats = 0; }

  // For benchmarking; load lazy-loaded expressions.
  static void Warm();

  // Operations with preconditions. The result is undefined
  // if the preconditions are not satisfied!

  // Computes x << 1, assuming x < 128.
  static Fluint8 LeftShift1Under128(Fluint8 x) {
    return Fluint8(x.h + x.h);
  }

  // Computes a + b, as long as the sum is < 256.
  static Fluint8 PlusNoOverflow(Fluint8 a, Fluint8 b) {
    return Fluint8(a.h + b.h);
  }

 private:
  // Evaluate the expression with the given value for the variable.
  static half_float::half Eval(const Exp *, half_float::half h);
  // static Fluint8 Eval(const Exp *, Fluint8 x);

  // Put in [-1,1) "choppy" form, or construct a fluint8 in
  // that form. Both are linear ops (just scale/offset).
  half_float::half ToChoppy() const;
  static Fluint8 FromChoppy(half_float::half h);

  explicit Fluint8(half_float::half h) : h(h) {}
  half_float::half h;

  // Internal expressions used for extracting bits.
  static const std::vector<const Exp *> &BitExps();

  // Compute bitwise AND.
  static half GetCommonBits(Fluint8 a, Fluint8 b);

  static half Canonicalize(half h);

  static constexpr inline uint16_t GetU16(half_float::half h) {
    return std::bit_cast<uint16_t, half_float::half>(h);
  }

  static constexpr inline half_float::half GetHalf(uint16_t u) {
    return std::bit_cast<half_float::half, uint16_t>(u);
  }

  static half RightShiftHalf1(half xh) {
    // Hand-made version. We divide, which give us almost the right
    // answer, and then round to an integer by shifting up by 1024
    // (where only integers are representable in half precision) and
    // back. The -0.25 is just a fudge factor so that we round in the
    // right direction.
    // return Fluint8((x.h * 0.5_h - 0.25_h) + 1024.0_h - 1024.0_h);

    // Same idea, but found through search; it just happens to work.
    // Here the fudge is built into the scale.
    // approximately 0.49853515625
    static constexpr half SCALE = GetHalf(0x37fa);
    // 1741
    static constexpr half OFFSET = GetHalf(0x66cd);
    return xh * SCALE + OFFSET - OFFSET;
  }

  static half RightShiftHalf2(half xh) {
    static constexpr half OFFSET1 = GetHalf(0xb54e);
    static constexpr half OFFSET2 = GetHalf(0x6417);
    return xh * (half)0.25f + OFFSET1 + OFFSET2 - OFFSET2;
  }

  static half RightShiftHalf3(half xh) {
    static constexpr half OFFSET1 = GetHalf(0xb642);
    static constexpr half OFFSET2 = GetHalf(0x67bc);
    return xh * (half)0.125f + OFFSET1 + OFFSET2 - OFFSET2;
  }

  // Works for integers in [0, 512); always results in 1 or 0.
  static half RightShiftHalf8(half xh) {
    static constexpr half OFFSET1 = GetHalf(0xb7f6);
    static constexpr half OFFSET2 = GetHalf(0x66b0);
    return xh * (half)(1.0f / 256.0f) + OFFSET1 + OFFSET2 - OFFSET2;
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

inline Fluint8 operator +(const Fluint8 &a, const Fluint8 &b) {
  return Fluint8::Plus(a, b);
}

inline Fluint8 operator -(const Fluint8 &a, const Fluint8 &b) {
  return Fluint8::Minus(a, b);
}

inline Fluint8 operator ^(const Fluint8 &a, const Fluint8 &b) {
  return Fluint8::BitwiseXor(a, b);
}

inline Fluint8 operator |(const Fluint8 &a, const Fluint8 &b) {
  return Fluint8::BitwiseOr(a, b);
}

inline Fluint8 operator &(const Fluint8 &a, const Fluint8 &b) {
  return Fluint8::BitwiseAnd(a, b);
}

inline Fluint8 operator +(const Fluint8 &a) {
  return a;
}

inline Fluint8 operator -(const Fluint8 &a) {
  return Fluint8::Minus(Fluint8(0), a);
}

inline Fluint8 operator ~(const Fluint8 &a) {
  return Fluint8::BitwiseXor(Fluint8(255), a);
}

inline Fluint8& operator++(Fluint8 &a) {
  a = Fluint8::Plus(a, Fluint8(1));
  return a;
}

inline Fluint8 operator++(Fluint8 &a, int) {
  Fluint8 ret = a;
  a = Fluint8::Plus(a, Fluint8(1));
  return ret;
}

inline Fluint8& operator--(Fluint8 &a) {
  a = Fluint8::Minus(a, Fluint8(1));
  return a;
}

inline Fluint8 operator--(Fluint8 &a, int) {
  Fluint8 ret = a;
  a = Fluint8::Minus(a, Fluint8(1));
  return ret;
}

inline Fluint8& operator+=(Fluint8 &a, const Fluint8 &b) {
  a = a + b;
  return a;
}

inline Fluint8& operator-=(Fluint8 &a, const Fluint8 &b) {
  a = a - b;
  return a;
}

inline Fluint8& operator^=(Fluint8 &a, const Fluint8 &b) {
  a = a ^ b;
  return a;
}

inline Fluint8& operator|=(Fluint8 &a, const Fluint8 &b) {
  a = a | b;
  return a;
}

inline Fluint8& operator&=(Fluint8 &a, const Fluint8 &b) {
  a = a & b;
  return a;
}


// Template implementations.
#if !FLUINT8_WRAP

template<size_t N>
Fluint8 Fluint8::LeftShift(Fluint8 x) {
  if constexpr (N == 0) {
    return x;
  } else {
    Fluint8 y = LeftShift<N - 1>(x);
    return y + y;
  }
}

template<size_t N>
Fluint8 Fluint8::RightShift(Fluint8 x) {
  using namespace half_float::literal;
  if constexpr (N == 0) {
    return x;
  } else if constexpr (N >= 4) {
    Fluint8 y = RightShift<N - 4>(x);
    // Similar idea to RightShift1, but found programmatically
    // with makeshift.cc.
    half z = ((y.h * (1.0_h / 16.0_h) - GetHalf(0x37b5)) +
              GetHalf(0x6630) - GetHalf(0x6630));
    return Fluint8(z);
  } else if constexpr (N >= 3) {
    Fluint8 y = RightShift<N - 3>(x);
    half z = RightShiftHalf3(y.h);
    return Fluint8(z);
  } else if constexpr (N >= 2) {
    Fluint8 y = RightShift<N - 2>(x);
    half z = RightShiftHalf2(y.h);
    return Fluint8(z);
  } else {
    Fluint8 y = RightShift<N - 1>(x);
    return RightShift1(y);
  }
}

template<uint8_t B>
Fluint8 Fluint8::AndWith(Fluint8 a) {
  // As in GetCommonBits, we compute the result directly in [0, 255]
  // space.

  // XXX unroll this so that it's clear that it's compile-time
  half common_bits = (half)0.0f;
  Fluint8 aa = a;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    // Low order bit as a - ((a >> 1) << 1)
    Fluint8 aashift = RightShift1(aa);

    if ((1 << bit_idx) & B) {
      // The shifted value does not have its high bit fast,
      // so we have a fast left shift (x + x).
      half bit = aa.h - LeftShift1Under128(aashift).h;
      // We know the bit from the constant b is 1, so copy the
      // bit from a.
      const half scale = (half)(1 << bit_idx);
      common_bits += scale * bit;
    } else {
      // Otherwise it is zero and contributes nothing to the output.
    }
    // and keep shifting down
    aa = aashift;
  }

  return Fluint8(common_bits);
}

template<uint8_t B>
Fluint8 Fluint8::OrWith(Fluint8 a) {
  half result = (half)0.0f;
  Fluint8 aa = a;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    // Low order bit as a - ((a >> 1) << 1)
    Fluint8 aashift = RightShift1(aa);

    const half scale = (half)(1 << bit_idx);
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

  return Fluint8(result);
}

template<uint8_t B>
Fluint8 Fluint8::XorWith(Fluint8 a) {
  half result = (half)0.0f;
  Fluint8 aa = a;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    // Low order bit as a - ((a >> 1) << 1)
    Fluint8 aashift = RightShift1(aa);
    half bit = aa.h - LeftShift1Under128(aashift).h;
    const half scale = (half)(1 << bit_idx);
    if ((1 << bit_idx) & B) {
      // Toggle the bit.
      result += scale * (1.0 - bit);
    } else {
      // Else copy the bit from a.
      result += scale * bit;
    }
    // and keep shifting down
    aa = aashift;
  }

  return Fluint8(result);
}

#endif

#endif


