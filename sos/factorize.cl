
// Careful! The builtin uint8 is a vector of 8 uints.
typedef uchar uint8_t;
typedef uint uint32_t;
typedef ulong uint64_t;
typedef long int64_t;
typedef atomic_uint atomic_uint32_t;

// This stuff should be defined by the C++ wrapper now:
// Three different methods: IsPrimeInternalOld, IsPrimeInternalUnrolled,
// IsPrimeInternalGeneral
// #define IsPrimeInternal IsPrimeInternalUnrolled
// These produce better code (to my eye) but aren't faster
// in benchmarks. Might be because they enable too much
// inlining or something like that.
// #define PTX_SUB128 0
// #define PTX_GEQ128 0
// #define PTX_MUL128 1

// #define BINV_USE_TABLE 1
// #define BINV_USE_DUMAS 1

// Avoid "dividing" twice in TRY. Code looks better but seems to
// benchmark worse. Could be because some peephole optimizations
// avoid the apparent problem with TRY.
// #define FUSED_TRY 0

// #define NEXT_PRIME 137
#if NEXT_PRIME <= 2
#  error NEXT_PRIME must be a nontrivial prime!
#endif

// This code is ported from cc-lib factorize.cc, which is in turn
// from gnu's factor utility.

void FactorizeInternal(uint64_t x,
                       uint64_t *factors,
                       int *num_factors,
                       bool *failed);

// First prime to not use for trial division (full factoring routine).
// This also affects the preconditions for the IsPrimeInternal call.


// Subtracts 128-bit words.
// returns high, low

// The original code generates:
//  setp.lt.u64   %p84, %rd2123, %rd139;
//  selp.b64  %rd1080, -1, 0, %p84;
//  sub.s64   %rd1081, %rd2122, %rd140;
//  add.s64   %rd2122, %rd1081, %rd1080;
//  sub.s64   %rd2123, %rd2123, %rd139;
// but we can do better with inline PTX. The
// sub.cc and subc instructions subtract with
// carry, exactly for this kind of thing.

inline ulong2
Sub128(uint64_t ah, uint64_t al,
       uint64_t bh, uint64_t bl) {
#if PTX_SUB128
  uint64_t rl, rh;
  asm("/* 128-bit subtract */\n\t"
      "sub.cc.u64 %0, %2, %3;\n\t"
      "subc.u64 %1, %4, %5;"
      :
      // outputs %0, %1
      "=l"(rl), // low word
      "=l"(rh)  // high word
      :
      // inputs %2, %3
      "l"(al),
      "l"(bl),
      // inputs %4, %5
      "l"(ah),
      "l"(bh));

  ulong2 ret;
  ret.s0 = rh;
  ret.s1 = rl;

  // asm("/* End Sub128 */");
  return ret;
#else
  uint64_t carry = al < bl;
  ulong2 ret;
  ret.s0 = ah - bh - carry;
  ret.s1 = al - bl;
  return ret;
#endif
}

// Right-shifts a 128-bit quantity by count.
// returns high, low
inline ulong2
RightShift128(uint64_t ah, uint64_t al, int count) {
  asm("/* Right Shift 128 */");
  ulong2 ret;
  ret.s0 = ah >> count;
  ret.s1 = (ah << (64 - count)) | (al >> count);
  asm("/* End Right Shift 128 */");
  return ret;
}

// This is maybe slightly shorter than the C code, but the
// real benefit of doing this with subtraction is that we
// immediately do the same 128-bit subtraction in UDiv128Rem,
// so we end up reusing the calculation.
inline bool GreaterEq128(uint64_t ah, uint64_t al,
                         uint64_t bh, uint64_t bl) {
#if PTX_GEQ128
  asm("/* geq 128 */");


  uint64_t rl, rh;
  asm("/* 128-bit subtract */\n\t"
      "sub.cc.u64 %0, %2, %3;\n\t"
      "subc.u64 %1, %4, %5;"
      :
      // outputs %0, %1
      "=l"(rl), // low word
      "=l"(rh)  // high word
      :
      // inputs %2, %3
      "l"(al),
      "l"(bl),
      // inputs %4, %5
      "l"(ah),
      "l"(bh));

  // a >= b iff a - b >= 0. We only need the high word (bit!)
  // to test this.
  bool res = ((int64_t)rh) >= 0;
  asm("/* end geq 128 */");
  return res;
#else
  return ah > bh || (ah == bh && al >= bl);
#endif
}

// Divides (n1*2^64 + n0)/d, with n1 < d. Returns remainder.
inline uint64_t UDiv128Rem(uint64_t n1,
                           uint64_t n0,
                           uint64_t d) {
  // assert (n1 < d);
  uint64_t d1 = d;
  uint64_t d0 = 0;
  uint64_t r1 = n1;
  uint64_t r0 = n0;
  for (unsigned int i = 64; i > 0; i--) {
    ulong2 dd = RightShift128(d1, d0, 1);
    d1 = dd.s0;
    d0 = dd.s1;
    if (GreaterEq128(r1, r0, d1, d0)) {
      ulong2 rr = Sub128(r1, r0, d1, d0);
      r1 = rr.s0;
      r0 = rr.s1;
    }
  }
  return r0;
}


/* x B (mod n).  */
inline uint64_t Redcify(uint64_t r, uint64_t n) {
  return UDiv128Rem(r, 0, n);
}

/* Requires that a < n and b <= n */
inline uint64_t SubMod(uint64_t a, uint64_t b, uint64_t n) {
  // PERF: Perhaps some trick with sum of absolute
  // difference?
  uint64_t t = - (uint64_t) (a < b);
  return (n & t) + a - b;
}

inline uint64_t AddMod(uint64_t a, uint64_t b, uint64_t n) {
  return SubMod(a, n - b, n);
}

#define ll_B ((uint64_t) 1 << (64 / 2))
#define ll_lowpart(t)  ((uint64_t) (t) & (ll_B - 1))
#define ll_highpart(t) ((uint64_t) (t) >> (64 / 2))

// PERF: Can be done with madc, etc.
//
// is it just as simple as
//  mul.lo.u64 l <- u, v
//  mul.hi.u64 h <- u, v
// ?
// returns (w1, w0)
inline ulong2 UMul128(uint64_t u, uint64_t v) {
#if PTX_MUL128
  uint64_t hi, lo;

  asm("mul.lo.u64 %0, %2, %3;\n\t"
      "mul.hi.u64 %1, %2, %3;\n\t"
      :
      // outputs %0, %1
      "=l"(lo), // low word
      "=l"(hi)  // high word
      :
      // inputs %2, %3
      "l"(u),
      "l"(v));

  ulong2 ret;
  ret.s0 = hi;
  ret.s1 = lo;
  return ret;
#else
  uint32_t ul = ll_lowpart(u);
  uint32_t uh = ll_highpart(u);
  uint32_t vl = ll_lowpart(v);
  uint32_t vh = ll_highpart(v);

  uint64_t x0 = (uint64_t) ul * vl;
  uint64_t x1 = (uint64_t) ul * vh;
  uint64_t x2 = (uint64_t) uh * vl;
  uint64_t x3 = (uint64_t) uh * vh;

  // This can't give carry.
  x1 += ll_highpart(x0);
  // But this indeed can.
  x1 += x2;
  // If so, add in the proper position.
  if (x1 < x2)
    x3 += ll_B;

  ulong2 ret;
  ret.s0 = x3 + ll_highpart(x1);
  ret.s1 = (x1 << 64 / 2) + ll_lowpart(x0);
  return ret;
#endif
}


#define NUM_PRIME_DELTAS 12
const uint8_t PRIME_DELTAS[NUM_PRIME_DELTAS] = {
  1,2,2,4,2,4,2,4,6,2,6,4,
};

// PERF: Benchmark Dumas's algorithm. It's alleged that it gives
// better instruction-level parallelism because of shorter dependency
// chains. See https://arxiv.org/pdf/2204.04342.pdf

#if BINV_USE_TABLE
/* Entry i contains (2i+1)^(-1) mod 2^8.  */
const unsigned char binvert_table[128] = {
  0x01, 0xAB, 0xCD, 0xB7, 0x39, 0xA3, 0xC5, 0xEF,
  0xF1, 0x1B, 0x3D, 0xA7, 0x29, 0x13, 0x35, 0xDF,
  0xE1, 0x8B, 0xAD, 0x97, 0x19, 0x83, 0xA5, 0xCF,
  0xD1, 0xFB, 0x1D, 0x87, 0x09, 0xF3, 0x15, 0xBF,
  0xC1, 0x6B, 0x8D, 0x77, 0xF9, 0x63, 0x85, 0xAF,
  0xB1, 0xDB, 0xFD, 0x67, 0xE9, 0xD3, 0xF5, 0x9F,
  0xA1, 0x4B, 0x6D, 0x57, 0xD9, 0x43, 0x65, 0x8F,
  0x91, 0xBB, 0xDD, 0x47, 0xC9, 0xB3, 0xD5, 0x7F,
  0x81, 0x2B, 0x4D, 0x37, 0xB9, 0x23, 0x45, 0x6F,
  0x71, 0x9B, 0xBD, 0x27, 0xA9, 0x93, 0xB5, 0x5F,
  0x61, 0x0B, 0x2D, 0x17, 0x99, 0x03, 0x25, 0x4F,
  0x51, 0x7B, 0x9D, 0x07, 0x89, 0x73, 0x95, 0x3F,
  0x41, 0xEB, 0x0D, 0xF7, 0x79, 0xE3, 0x05, 0x2F,
  0x31, 0x5B, 0x7D, 0xE7, 0x69, 0x53, 0x75, 0x1F,
  0x21, 0xCB, 0xED, 0xD7, 0x59, 0xC3, 0xE5, 0x0F,
  0x11, 0x3B, 0x5D, 0xC7, 0x49, 0x33, 0x55, 0xFF,
};

inline uint64_t Binv8bits(uint64_t n) {
  return binvert_table[(n >> 1) & 0x7F];
}

#else

inline uint64_t Binv8bits(uint64_t n) {
  // 4 bits correct.
  uint64_t x0 = (3 * n) ^ 0x02;
  uint64_t x1 = 2 * x0 - x0 * x0 * n;

  // or dumas-style?
  // PERF I think we only would get the benefit if we inline
  // it into Binv.
  // uint64_t y = 1 - a * x0;
  // uint64_t x1 = x0 * (1 + y);
  return x1;
}

#endif


#if BINV_USE_DUMAS

#if BINV_USE_TABLE
inline uint64_t Binv(uint64_t n) {
  uint64_t x0 = Binv8bits(n);
  uint64_t y = 1 - n * x0;
  uint64_t x1 = x0 * (1 + y);
  y *= y;
  uint64_t x2 = x1 * (1 + y);
  y *= y;
  uint64_t x3 = x2 * (1 + y);
  return x3;
}
#else

// Not simple to call Binv8bits in this case,
// since we want to retain the intermediate
// values of y.
uint64_t Binv(uint64_t n) {
  uint64_t x0 = (3 * n) ^ 0x02;
  uint64_t y = 1 - n * x0;
  uint64_t x1 = x0 * (1 + y);
  y *= y;
  uint64_t x2 = x1 * (1 + y);
  y *= y;
  uint64_t x3 = x2 * (1 + y);
  y *= y;
  uint64_t x4 = x3 * (1 + y);
  return x4;
}

#endif

#else

inline uint64_t Binv(uint64_t n) {
  uint64_t inv = Binv8bits(n);
  inv = 2 * inv - inv * inv * n;
  inv = 2 * inv - inv * inv * n;
  inv = 2 * inv - inv * inv * n;
  return inv;
}

#endif

// PERF: This can be done with PTX that uses carries.
/* Modular two-word multiplication, r = a * b mod m, with mi = m^(-1) mod B.
   Both a and b must be in redc form, the result will be in redc form too.

   (Redc is "montgomery form". mi stands for modular inverse.
    See https://en.wikipedia.org/wiki/Montgomery_modular_multiplication )
*/
inline uint64_t
MulRedc(uint64_t a, uint64_t b, uint64_t m, uint64_t mi) {
  ulong2 r = UMul128(a, b);
  uint64_t rh = r.s0;
  uint64_t rl = r.s1;
  uint64_t q = rl * mi;
  // PERF: Can just compute mul.hi if that's all we're using
  uint64_t th = UMul128(q, m).s0;
  uint64_t xh = rh - th;
  if (rh < th)
    xh += m;

  return xh;
}

// computes b^e mod n. b in redc form. result in redc form.
uint64_t
PowM(uint64_t b, uint64_t e, uint64_t n, uint64_t ni, uint64_t one) {
  uint64_t y = one;

  if (e & 1)
    y = b;

  // PERF: for OpenCL, we might want to just run this loop a fixed
  // number (63 I think) of times? In the unrolled version we could
  // use a bit test instruction instead of shifting and anding, although
  // those are about as cheap as it gets.
  while (e != 0) {
    b = MulRedc(b, b, n, ni);
    e >>= 1;

    if (e & 1)
      y = MulRedc(y, b, n, ni);
  }

  return y;
}

inline uint64_t HighBitToMask(uint64_t x) {
  // This requires an arithmetic shift, which appears to be the
  // OpenCL behavior:
  // registry.khronos.org/OpenCL/specs/3.0-unified/html/OpenCL_C.html
  //    #operators-shift
  return (uint64_t)((int64_t)(x) >> (64 - 1));
}

uint64_t GCDOdd(uint64_t a, uint64_t b) {
  if ((b & 1) == 0) {
    uint64_t t = b;
    b = a;
    a = t;
  }
  if (a == 0)
    return b;

  /* Take out least significant one bit, to make room for sign */
  b >>= 1;

  for (;;) {
    // remove trailing zeroes from a.
    int zeroes = ctz(a);
    a >>= zeroes + 1;
    /*
    while ((a & 1) == 0)
      a >>= 1;
    a >>= 1;
    */

    uint64_t t = a - b;
    if (t == 0)
      return (a << 1) + 1;

    // I think this stands for "b greater than a" -tom7
    // Seems like they're trying to do branchless tricks here
    // but we can probably just use builtins.
    uint64_t bgta = HighBitToMask(t);

    /* b <-- min (a, b) */
    // PERF: min
    b += (bgta & t);

    /* a <-- |a - b| */
    // PERF: sum of absolute differences
    a = (t ^ bgta) - bgta;
  }
}

// One Miller-Rabin test. Returns true if definitely composite;
// false if maybe prime.
// one and nm1 are just 1 and n-1 those numbers in redc form.
bool DefinitelyComposite(uint64_t n, uint64_t ni, uint64_t b, uint64_t q,
                         unsigned int k, uint64_t one, uint64_t nm1) {
  asm("/* definitelycomposite */");
  uint64_t y = PowM(b, q, n, ni, one);

  if (y == one || y == nm1)
    return false;

  // PERF idea: Could do this simultaneously for all bases?
  // No clear reason why it would be better, but it might be
  // worth testing.
  for (unsigned int i = 1; i < k; i++) {
    // y = y^2 mod n
    y = MulRedc(y, y, n, ni);

    if (y == nm1)
      return false;
    if (y == one)
      return true;
  }
  return true;
}

bool IsPrimeInternalOld(uint64_t n) {
  if (n <= 1)
    return false;

  /* We have already sieved out small primes.
     This also means that we don't need to check a = n as
     we consider the bases below. */
  if (n < (uint64_t)(NEXT_PRIME * NEXT_PRIME))
    return true;

  // Precomputation.
  uint64_t q = n - 1;
  // Count and remove trailing zeroes.
  int k = ctz(q);
  q >>= k;

  const uint64_t ni = Binv(n);                 /* ni <- 1/n mod B */
  const uint64_t one = Redcify(1, n);
  uint64_t a_prim = AddMod(one, one, n); /* i.e., redcify a = 2 */
  int a = 2;

  /* -1, but in redc representation. */
  uint64_t nm1 = n - one;

  // Just need to check the first 12 prime bases for 64-bit ints.
  for (int i = 0; i < 12; i++) {
    if (DefinitelyComposite(n, ni, a_prim, q, k, one, nm1))
      return false;

    uint8_t delta = PRIME_DELTAS[i];

    // Establish new base.
    a += delta;

    /* The following is equivalent to a_prim = redcify (a, n).  It runs faster
       on most processors, since it avoids udiv128. */
    {
      ulong2 r = UMul128(one, a);
      if (r.s0 == 0) {
        a_prim = r.s1 % n;
      } else {
        a_prim = UDiv128Rem(r.s0, r.s1, n);
      }
    }
  }

  // The test above detects all 64-bit composite numbers, so this
  // must be a prime.
  return true;
}

bool IsPrimeInternalUnrolled(uint64_t n) {
  asm("/* isprimeinternalunrolled */");

  if (n <= 1)
    return false;

  /* We have already sieved out small primes.
     This also means that we don't need to check a = n as
     we consider the bases below. */
  if (n < (uint64_t)(NEXT_PRIME * NEXT_PRIME))
    return true;

  // Precomputation.
  uint64_t q = n - 1;
  // Count and remove trailing zeroes.
  int k = ctz(q);
  q >>= k;

  const uint64_t ni = Binv(n);                 /* ni <- 1/n mod B */
  const uint64_t one = Redcify(1, n);
  /* -1, but in redc representation. */
  const uint64_t nm1 = n - one;

  // We just need to check 12 small primes here, so this code is
  // totally unrolled. For each prime, we need to compute its
  // redc representation. Redcify requires UDiv128, but addition in
  // this form is fast, so we construct them this way from other
  // numbers that we've already computed.

  // primes: 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37
  // deltas:  1,2,2,4,2,4,2,4,6,2,6,4,
  // We can build each one with a single addition, although
  // we need to compute six to do that.

  // Note that AddMod has the precondition that the numbers being
  // added are less than n, which will be true because of the
  // check above that n >= NEXT_PRIME^2. (XXX Except for extreme
  // tunings!)
  uint64_t two = AddMod(one, one, n);
  if (DefinitelyComposite(n, ni, two, q, k, one, nm1))
    return false;

  uint64_t three = AddMod(two, one, n);
  if (DefinitelyComposite(n, ni, three, q, k, one, nm1))
    return false;

  uint64_t five = AddMod(three, two, n);
  if (DefinitelyComposite(n, ni, five, q, k, one, nm1))
    return false;

  uint64_t seven = AddMod(five, two, n);
  if (DefinitelyComposite(n, ni, seven, q, k, one, nm1))
    return false;

  // Not prime, but needed to create numbers below.
  uint64_t six = AddMod(three, three, n);

  uint64_t eleven = AddMod(six, five, n);
  if (DefinitelyComposite(n, ni, eleven, q, k, one, nm1))
    return false;

  uint64_t thirteen = AddMod(eleven, two, n);
  if (DefinitelyComposite(n, ni, thirteen, q, k, one, nm1))
    return false;

  uint64_t seventeen = AddMod(eleven, six, n);
  if (DefinitelyComposite(n, ni, seventeen, q, k, one, nm1))
    return false;

  uint64_t nineteen = AddMod(seventeen, two, n);
  if (DefinitelyComposite(n, ni, nineteen, q, k, one, nm1))
    return false;

  uint64_t twenty_three = AddMod(seventeen, six, n);
  if (DefinitelyComposite(n, ni, twenty_three, q, k, one, nm1))
    return false;

  uint64_t twenty_nine = AddMod(twenty_three, six, n);
  if (DefinitelyComposite(n, ni, twenty_nine, q, k, one, nm1))
    return false;

  uint64_t thirty_one = AddMod(twenty_nine, two, n);
  if (DefinitelyComposite(n, ni, thirty_one, q, k, one, nm1))
    return false;

  uint64_t thirty_seven = AddMod(thirty_one, six, n);
  if (DefinitelyComposite(n, ni, thirty_seven, q, k, one, nm1))
    return false;

  // Test above is comprehensive for 64-bit numbers.
  return true;
};

static const uint32_t WITNESSES[7] =
  { 2, 325, 9375, 28178, 450775, 9780504, 1795265022 };

// No preconditions.
bool IsPrimeInternalGeneral(uint64_t n) {
  if (n <= 1)
    return false;

  // PERF: If we're using this through the full factorization
  // test, we should test vs NEXT_PRIME^2 first.
  if (n == 2) return true;
  if (n == 3) return true;
  if (n == 5) return true;
  if (n == 13) return true;
  if (n == 19) return true;
  if (n == 73) return true;
  if (n == 193) return true;
  if (n == 407521) return true;
  if (n == 299210837) return true;

  // Precomputation.
  uint64_t q = n - 1;
  // Count and remove trailing zeroes.
  int k = ctz(q);
  q >>= k;

  const uint64_t ni = Binv(n);                 /* ni <- 1/n mod B */
  const uint64_t one = Redcify(1, n);

  // This assumes WITNESSES[0] == 2.
  uint64_t a_prim = AddMod(one, one, n); /* i.e., redcify a = 2 */
  int a = 2;

  /* -1, but in redc representation. */
  const uint64_t nm1 = n - one;


  if (DefinitelyComposite(n, ni, a_prim, q, k, one, nm1))
    return false;

  // Skipping index 0, which we just did.
  for (int i = 1; i < 7; i++) {
    // Establish new base.
    a = WITNESSES[i];

    // compute a_prim from a
    {
      // PERF: Compiler may do this for us, but we can just
      // compute the hi part of the multiplication to start;
      // the lo part is often unused.
      ulong2 r = UMul128(one, a);
      if (r.s0 == 0) {
        a_prim = r.s1 % n;
      } else {
        a_prim = UDiv128Rem(r.s0, r.s1, n);
      }
    }

    if (DefinitelyComposite(n, ni, a_prim, q, k, one, nm1)) {
      return false;
    }
  }

  // The test above detects all 64-bit composite numbers, so this
  // must be a prime.
  return true;
}

// This version requires all factors below NEXT_PRIME to have
// been sieved out, and I tried to tune it as well.
// I think this one might also be incorrect (see below).
bool IsPrimeInternalSuspicious(uint64_t n) {
  if (n < NEXT_PRIME * NEXT_PRIME) {
    if (n <= 1)
      return false;
    return true;
  }

  // PERF: Assuming these are all optimized away, but check!
  if (n == 2) return true;
  if (n == 3) return true;
  if (n == 5) return true;
  if (n == 13) return true;
  if (n == 19) return true;
  if (n == 73) return true;
  if (n == 193) return true;
  if (n == 407521) return true;
  if (n == 299210837) return true;

  // Precomputation.
  uint64_t q = n - 1;
  // Count and remove trailing zeroes.
  int k = ctz(q);
  q >>= k;

  /* ni <- 1/n mod B */
  const uint64_t ni = Binv(n);
  const uint64_t redc1 = Redcify(1, n);

  // PERF: Here we're trying to construct the redc representation of
  // all the witness numbers (2, 325, 9375, 28178, 450775, 9780504, 1795265022)
  // by using addition, since this straight line code (Redc involves
  // a loop). There is probably a faster approach than binary, and
  // we can use subtraction too.
  //
  // However FIXME: We might be violating the precondition of AddMod, which
  // wants both arguments to be less than n. This is fine for small witnesses
  // (less than NEXT_PRIME^2) but the last of these are quite big.
  const uint64_t redc2 = AddMod(redc1, redc1, n);
  const uint64_t redc4 = AddMod(redc2, redc2, n);
  const uint64_t redc8 = AddMod(redc4, redc4, n);
  const uint64_t redc16 = AddMod(redc8, redc8, n);
  const uint64_t redc32 = AddMod(redc16, redc16, n);
  const uint64_t redc64 = AddMod(redc32, redc32, n);
  const uint64_t redc128 = AddMod(redc64, redc64, n);
  const uint64_t redc256 = AddMod(redc128, redc128, n);
  const uint64_t redc512 = AddMod(redc256, redc256, n);
  const uint64_t redc1024 = AddMod(redc512, redc512, n);
  const uint64_t redc2048 = AddMod(redc1024, redc1024, n);
  const uint64_t redc4096 = AddMod(redc2048, redc2048, n);
  const uint64_t redc8192 = AddMod(redc4096, redc4096, n);
  const uint64_t redc16384 = AddMod(redc8192, redc8192, n);
  const uint64_t redc32768 = AddMod(redc16384, redc16384, n);
  const uint64_t redc65536 = AddMod(redc32768, redc32768, n);
  const uint64_t redc131072 = AddMod(redc65536, redc65536, n);
  const uint64_t redc262144 = AddMod(redc131072, redc131072, n);
  const uint64_t redc524288 = AddMod(redc262144, redc262144, n);
  const uint64_t redc1048576 = AddMod(redc524288, redc524288, n);
  const uint64_t redc2097152 = AddMod(redc1048576, redc1048576, n);
  const uint64_t redc4194304 = AddMod(redc2097152, redc2097152, n);
  const uint64_t redc8388608 = AddMod(redc4194304, redc4194304, n);

  // 8388608
  const uint64_t redc16777216 = AddMod(redc8388608, redc8388608, n);
  // 16777216
  const uint64_t redc33554432 = AddMod(redc16777216, redc16777216, n);
  // 33554432
  const uint64_t redc67108864 = AddMod(redc33554432, redc33554432, n);
  // 67108864
  const uint64_t redc134217728 = AddMod(redc67108864, redc67108864, n);
  // 134217728
  const uint64_t redc268435456 = AddMod(redc134217728, redc134217728, n);
  // 268435456
  const uint64_t redc536870912 = AddMod(redc268435456, redc268435456, n);
  // 536870912
  const uint64_t redc1073741824 = AddMod(redc536870912, redc536870912, n);
  // 1073741824


  /* -1, but in redc representation. */
  const uint64_t nm1 = n - redc1;

  // Unroll the first few, since we can compute a_prim
  // easily for those.
  if (DefinitelyComposite(n, ni, redc2, q, k, redc1, nm1))
    return false;

  // { 2, 325, 9375, 28178, 450775, 9780504, 1795265022 };


  #if 325 != 1 + 4 + 64 + 256
  #error 325
  #endif
  // 325:
  //    21
  //    52631
  //    684268421
  // 000101000101
  const uint64_t redc325 =
    AddMod(redc1,
           AddMod(redc4,
                  AddMod(redc64,
                         redc256, n), n), n);
  if (DefinitelyComposite(n, ni, redc325, q, k, redc1, nm1))
    return false;

  // 0010010010011111
  // 9375
  uint64_t redc9375 =
    AddMod(redc1, AddMod(redc2, AddMod(redc4, AddMod(redc8, AddMod(redc16, AddMod(redc128, AddMod(redc1024, redc8192, n), n), n), n), n), n), n);

  if (DefinitelyComposite(n, ni, redc9375, q, k, redc1, nm1))
    return false;

  // 28178
  uint64_t redc28178 =
    AddMod(redc2, AddMod(redc16, AddMod(redc512, AddMod(redc1024, AddMod(redc2048, AddMod(redc8192, redc16384, n), n), n), n), n), n);

  if (DefinitelyComposite(n, ni, redc28178, q, k, redc1, nm1))
    return false;

  // 450775
  uint64_t redc450775 =
    AddMod(redc1, AddMod(redc2, AddMod(redc4, AddMod(redc16, AddMod(redc64, AddMod(redc128, AddMod(redc8192, AddMod(redc16384, AddMod(redc32768, AddMod(redc131072, redc262144, n), n), n), n), n), n), n), n), n), n);

  if (DefinitelyComposite(n, ni, redc450775, q, k, redc1, nm1))
    return false;

  // 9780504
  uint64_t redc9780504 =
    AddMod(redc8, AddMod(redc16, AddMod(redc256, AddMod(redc1024, AddMod(redc2048, AddMod(redc4096, AddMod(redc8192, AddMod(redc65536, AddMod(redc262144, AddMod(redc1048576, redc8388608, n), n), n), n), n), n), n), n), n), n);

  if (DefinitelyComposite(n, ni, redc9780504, q, k, redc1, nm1))
    return false;

  // 1795265022
  uint64_t redc1795265022 =
    AddMod(redc2, AddMod(redc4, AddMod(redc8, AddMod(redc16, AddMod(redc32, AddMod(redc64, AddMod(redc128, AddMod(redc256, AddMod(redc4096, AddMod(redc32768, AddMod(redc65536, AddMod(redc16777216, AddMod(redc33554432, AddMod(redc134217728, AddMod(redc536870912, redc1073741824, n), n), n), n), n), n), n), n), n), n), n), n), n), n), n);

  if (DefinitelyComposite(n, ni, redc1795265022, q, k, redc1, nm1))
    return false;

  // The test above detects all 64-bit composite numbers, so this
  // must be a prime.
  return true;
}

// Fixed
bool IsPrimeInternalFew(uint64_t n) {
  if (n < NEXT_PRIME * NEXT_PRIME) {
    if (n <= 1)
      return false;
    return true;
  }

  // PERF: Assuming these are all optimized away, but check!
  if (n == 2) return true;
  if (n == 3) return true;
  if (n == 5) return true;
  if (n == 13) return true;
  if (n == 19) return true;
  if (n == 73) return true;
  if (n == 193) return true;
  if (n == 407521) return true;
  if (n == 299210837) return true;

  // Precomputation.
  uint64_t q = n - 1;
  // Count and remove trailing zeroes.
  int k = ctz(q);
  q >>= k;

  /* ni <- 1/n mod B */
  const uint64_t ni = Binv(n);
  const uint64_t one = Redcify(1, n);

  // This assumes WITNESSES[0] == 2.
  const uint64_t two = AddMod(one, one, n); /* i.e., redcify a = 2 */

  /* -1, but in redc representation. */
  const uint64_t nm1 = n - one;

  if (DefinitelyComposite(n, ni, two, q, k, one, nm1))
    return false;

  // Maybe this one could be done with the tricks below...
  {
    const uint64_t a = 325;
    uint64_t a_prim = 0;

    {
      ulong2 r = UMul128(one, a);
      if (r.s0 == 0) {
        a_prim = r.s1 % n;
      } else {
        a_prim = UDiv128Rem(r.s0, r.s1, n);
      }
    }

    if (DefinitelyComposite(n, ni, a_prim, q, k, one, nm1)) {
      return false;
    }
  }

  {
    const uint64_t a = 9375;
    uint64_t a_prim = 0;

    {
      ulong2 r = UMul128(one, a);
      if (r.s0 == 0) {
        a_prim = r.s1 % n;
      } else {
        a_prim = UDiv128Rem(r.s0, r.s1, n);
      }
    }

    if (DefinitelyComposite(n, ni, a_prim, q, k, one, nm1)) {
      return false;
    }
  }

  {
    const uint64_t a = 28178;
    uint64_t a_prim = 0;

    {
      ulong2 r = UMul128(one, a);
      if (r.s0 == 0) {
        a_prim = r.s1 % n;
      } else {
        a_prim = UDiv128Rem(r.s0, r.s1, n);
      }
    }

    if (DefinitelyComposite(n, ni, a_prim, q, k, one, nm1)) {
      return false;
    }
  }

  {
    const uint64_t a = 450775;
    uint64_t a_prim = 0;

    {
      ulong2 r = UMul128(one, a);
      if (r.s0 == 0) {
        a_prim = r.s1 % n;
      } else {
        a_prim = UDiv128Rem(r.s0, r.s1, n);
      }
    }

    if (DefinitelyComposite(n, ni, a_prim, q, k, one, nm1)) {
      return false;
    }
  }

  {
    const uint64_t a = 9780504;
    uint64_t a_prim = 0;

    {
      ulong2 r = UMul128(one, a);
      if (r.s0 == 0) {
        a_prim = r.s1 % n;
      } else {
        a_prim = UDiv128Rem(r.s0, r.s1, n);
      }
    }

    if (DefinitelyComposite(n, ni, a_prim, q, k, one, nm1)) {
      return false;
    }
  }

  {
    const uint64_t a = 1795265022;
    uint64_t a_prim = 0;

    {
      ulong2 r = UMul128(one, a);
      if (r.s0 == 0) {
        a_prim = r.s1 % n;
      } else {
        a_prim = UDiv128Rem(r.s0, r.s1, n);
      }
    }

    if (DefinitelyComposite(n, ni, a_prim, q, k, one, nm1)) {
      return false;
    }
  }


  // The test above detects all 64-bit composite numbers, so this
  // must be a prime.
  return true;
}


void FactorUsingPollardRho(uint64_t n,
                           uint64_t a,
                           uint64_t *factors,
                           int *num_factors,
                           bool *failed) {

 restart:;
  // printf("rho(%llu, %llu)\n", n, a);

  uint64_t g;

  unsigned long int k = 1;
  unsigned long int l = 1;

  uint64_t P = Redcify(1, n);
  // i.e., Redcify(2)
  uint64_t x = AddMod(P, P, n);
  uint64_t z = x;
  uint64_t y = x;

  while (n != 1) {
    // assert (a < n);
    // Just bail if it's taking too many tests.
    // This threshold is tunable (maybe it should even just be 1,
    // i.e., no loop!)
    // at 10: 2m43s
    // at 2: 2m43s
    // at 1: 2m53s
    if (a > 2) {
      // XXX
      *failed = true;
      return;
    }

    const uint64_t ni = Binv(n);

    for (;;) {
      do {
        x = MulRedc(x, x, n, ni);
        x = AddMod(x, a, n);

        const uint64_t t = SubMod(z, x, n);
        P = MulRedc(P, t, n, ni);

        if (k % 32 == 1) {
          if (GCDOdd(P, n) != 1)
            goto factor_found;
          y = x;
        }
      } while (--k != 0);

      z = x;
      k = l;
      l = 2 * l;
      for (unsigned long int i = 0; i < k; i++) {
        x = MulRedc(x, x, n, ni);
        x = AddMod(x, a, n);
      }
      y = x;
    }

  factor_found:
    do {
      y = MulRedc(y, y, n, ni);
      y = AddMod(y, a, n);

      uint64_t t = SubMod(z, y, n);
      g = GCDOdd(t, n);
    } while (g == 1);

    if (n == g) {
      /* Found n itself as factor.  Restart with different params. */

      // printf("recurse\n");
      // return;

      a++;
      // printf("restart w/ n == g\n");
      goto restart;
    }

    n = n / g;

    bool n_prime = IsPrimeInternal(n);
    bool g_prime = IsPrimeInternal(g);

    if (n_prime && g_prime) {
      factors[*num_factors] = n;
      ++*num_factors;
      factors[*num_factors] = g;
      ++*num_factors;
      return;
    } else if (n_prime) {
      // continue with g.
      factors[*num_factors] = n;
      ++*num_factors;
      n = g;
      a++;
      goto restart;
    } else if (g_prime) {
      factors[*num_factors] = g;
      ++*num_factors;

      // Continue working on n.
      x = x % n;
      z = z % n;
      y = y % n;
    } else {
      // would need some kind of stack or recursion.
      // we do have room for it (for example we could
      // store composite factors at the end of the
      // factors array, and then just keep a count),
      // but this routine already succeeds on 99.93% of
      // random uint64s. So if anything we probably
      // want to handle *fewer* cases if we can make
      // it sufficiently faster.
      *failed = true;
      return;
    }
  }
}

// Core of the kernel.
void FactorizeInternal(uint64_t x,
                       uint64_t *factors,
                       int *num_factors,
                       bool *failed) {
  if (x <= 1) {
    return;
  }

  int nf = *num_factors;

  uint64_t cur = x;

  const int twos = ctz(x);
  if (twos) {
    for (int i = 0; i < twos; i++) {
      factors[nf++] = 2;
    }
    cur >>= twos;
  }

#if FUSED_TRY
  #define TRY(p) do { if (p < NEXT_PRIME) {       \
      uint64_t q = cur / p;                       \
      uint64_t qd = q * p;                        \
      uint64_t rem = cur - qd;                    \
      while (rem == 0) {                          \
        cur = q;                                  \
        factors[nf++] = p;                        \
        q = cur / p;                              \
        qd = q * p;                               \
        rem = cur - qd;                           \
      }                                           \
    } } while(0)

#else

  // The % and / by p here don't get fused in the PTX code, so we get
  // two divisions (although it's possible that some (invisible to
  // me) peephole phase fixes it). NEW_TRY avoids that but also
  // benchmarks slower.
  #define TRY(p) do { if (p < NEXT_PRIME) {       \
          while (cur % p == 0) {                  \
            cur /= p;                             \
            factors[nf++] = p;                    \
          }                                       \
  } } while (0)

#endif

  // TRY(2);

  // PERF: For small factors like 3, we could first do a looped
  // try with an argument of 9, and then a single test for a
  // remaining factor of 3.
  TRY(3);
  TRY(5);
  TRY(7);
  TRY(11);
  TRY(13);
  TRY(17);
  TRY(19);
  TRY(23);
  TRY(29);
  TRY(31);
  TRY(37);
  TRY(41);
  TRY(43);
  TRY(47);
  TRY(53);
  TRY(59);
  TRY(61);
  TRY(67);
  TRY(71);
  TRY(73);
  TRY(79);
  TRY(83);
  TRY(89);
  TRY(97);
  TRY(101);
  TRY(103);
  TRY(107);
  TRY(109);
  TRY(113);
  TRY(127);
  TRY(131);
  // PERF: Once these get big enough, we can start unrolling the
  // loop completely. 131^10 > 2^64, for example. You can actually
  // exhaust the space with fewer divisions, like,
  //  if (n is divisible by 131^5) { n /= 131^5; factors[131] += 5; }
  //  // now n has at most 4 factors of 131
  //  if (n is divisible by 131^2) { n /= 131^2; factors[131] += 2; }
  //  // now n has at most 2 factors of 131
  //  if (n is divisible by 131) { n /= 131; factors[131]++; }
  //  if (n is divisible by 131) { n /= 131; factors[131]++; }

  TRY(137);
  TRY(139);
  TRY(149);
  TRY(151);
  TRY(157);
  TRY(163);
  TRY(167);
  TRY(173);
  TRY(179);
  TRY(181);
  TRY(191);
  TRY(193);
  TRY(197);
  TRY(199);
  TRY(211);
  TRY(223);
  TRY(227);
  TRY(229);
  TRY(233);
  TRY(239);
  TRY(241);
  TRY(251);
  TRY(257);
  TRY(263);
  TRY(269);
  TRY(271);
  TRY(277);
  TRY(281);
  TRY(283);
  TRY(293);
  TRY(307);
  TRY(311);
  TRY(313);
  TRY(317);
  TRY(331);
  TRY(337);
  TRY(347);
  TRY(349);
  TRY(353);
  TRY(359);
  TRY(367);
  TRY(373);
  TRY(379);
  TRY(383);
  TRY(389);
  TRY(397);
  TRY(401);
  TRY(409);
  TRY(419);
  TRY(421);
  TRY(431);
  TRY(433);
  TRY(439);
  TRY(443);
  TRY(449);
  TRY(457);
  TRY(461);
  TRY(463);
  TRY(467);
  TRY(479);
  TRY(487);
  TRY(491);
  TRY(499);
  TRY(503);
  TRY(509);
  TRY(521);
  TRY(523);
  TRY(541);

#if NEXT_PRIME > 541

  TRY(547);
  TRY(557);
  TRY(563);
  TRY(569);
  TRY(571);
  TRY(577);
  TRY(587);
  TRY(593);
  TRY(599);
  TRY(601);
  TRY(607);
  TRY(613);
  TRY(617);
  TRY(619);
  TRY(631);
  TRY(641);
  TRY(643);
  TRY(647);
  TRY(653);
  TRY(659);
  TRY(661);
  TRY(673);
  TRY(677);
  TRY(683);
  TRY(691);
  TRY(701);
  TRY(709);
  TRY(719);
  TRY(727);
  TRY(733);
  TRY(739);
  TRY(743);
  TRY(751);
  TRY(757);
  TRY(761);
  TRY(769);
  TRY(773);
  TRY(787);
  TRY(797);
  TRY(809);
  TRY(811);
  TRY(821);
  TRY(823);
  TRY(827);
  TRY(829);
  TRY(839);
  TRY(853);
  TRY(857);
  TRY(859);
  TRY(863);
  TRY(877);
  TRY(881);
  TRY(883);
  TRY(887);
  TRY(907);
  TRY(911);
  TRY(919);
  TRY(929);
  TRY(937);
  TRY(941);
  TRY(947);
  TRY(953);
  TRY(967);
  TRY(971);
  TRY(977);
  TRY(983);
  TRY(991);
  TRY(997);
  TRY(1009);
  TRY(1013);
  TRY(1019);
  TRY(1021);
  TRY(1031);
  TRY(1033);
  TRY(1039);
  TRY(1049);
  TRY(1051);
  TRY(1061);
  TRY(1063);
  TRY(1069);
  TRY(1087);
  TRY(1091);
  TRY(1093);
  TRY(1097);
  TRY(1103);
  TRY(1109);
  TRY(1117);
  TRY(1123);
  TRY(1129);
  TRY(1151);
  TRY(1153);
  TRY(1163);
  TRY(1171);
  TRY(1181);
  TRY(1187);
  TRY(1193);
  TRY(1201);
  TRY(1213);
  TRY(1217);
  TRY(1223);
  TRY(1229);
  TRY(1231);
  TRY(1237);
  TRY(1249);
  TRY(1259);
  TRY(1277);
  TRY(1279);
  TRY(1283);
  TRY(1289);
  TRY(1291);
  TRY(1297);
  TRY(1301);
  TRY(1303);
  TRY(1307);
  TRY(1319);
  TRY(1321);
  TRY(1327);
  TRY(1361);
  TRY(1367);
  TRY(1373);
  TRY(1381);
  TRY(1399);
  TRY(1409);
  TRY(1423);
  TRY(1427);
  TRY(1429);
  TRY(1433);
  TRY(1439);
  TRY(1447);
  TRY(1451);
  TRY(1453);
  TRY(1459);
  TRY(1471);
  TRY(1481);
  TRY(1483);
  TRY(1487);
  TRY(1489);
  TRY(1493);
  TRY(1499);
  TRY(1511);
  TRY(1523);
  TRY(1531);
  TRY(1543);
  TRY(1549);
  TRY(1553);
  TRY(1559);
  TRY(1567);
  TRY(1571);
  TRY(1579);
  TRY(1583);
  TRY(1597);
  TRY(1601);
  TRY(1607);
  TRY(1609);
  TRY(1613);
  TRY(1619);
  TRY(1621);
  TRY(1627);
  TRY(1637);
  TRY(1657);
  TRY(1663);
  TRY(1667);
  TRY(1669);
  TRY(1693);
  TRY(1697);
  TRY(1699);
  TRY(1709);
  TRY(1721);
  TRY(1723);
  TRY(1733);
  TRY(1741);
  TRY(1747);
  TRY(1753);
  TRY(1759);
  TRY(1777);
  TRY(1783);
  TRY(1787);
  TRY(1789);
  TRY(1801);
  TRY(1811);
  TRY(1823);
  TRY(1831);
  TRY(1847);
  TRY(1861);
  TRY(1867);
  TRY(1871);
  TRY(1873);
  TRY(1877);
  TRY(1879);
  TRY(1889);
  TRY(1901);
  TRY(1907);
  TRY(1913);
  TRY(1931);
  TRY(1933);
  TRY(1949);
  TRY(1951);
  TRY(1973);
  TRY(1979);
  TRY(1987);
  TRY(1993);
  TRY(1997);
  TRY(1999);
  TRY(2003);
  TRY(2011);
  TRY(2017);
  TRY(2027);
  TRY(2029);
  TRY(2039);
  TRY(2053);
  TRY(2063);
  TRY(2069);
  TRY(2081);
  TRY(2083);
  TRY(2087);
  TRY(2089);
  TRY(2099);
  TRY(2111);
  TRY(2113);
  TRY(2129);
  TRY(2131);
  TRY(2137);
  TRY(2141);
  TRY(2143);
  TRY(2153);
  TRY(2161);
  TRY(2179);
  TRY(2203);
  TRY(2207);
  TRY(2213);
  TRY(2221);
  TRY(2237);
  TRY(2239);
  TRY(2243);
  TRY(2251);
  TRY(2267);
  TRY(2269);
  TRY(2273);
  TRY(2281);
  TRY(2287);
  TRY(2293);
  TRY(2297);
  TRY(2309);
  TRY(2311);
  TRY(2333);
  TRY(2339);
  TRY(2341);
  TRY(2347);
  TRY(2351);
  TRY(2357);
  TRY(2371);
  TRY(2377);
  TRY(2381);
  TRY(2383);
  TRY(2389);
  TRY(2393);
  TRY(2399);
  TRY(2411);

#if NEXT_PRIME > 2411
# error Would need to add more TRY cases.
#endif

#endif

#undef TRY

  // printf("End TRY\n");

  if (cur != 1) {
    // printf("Is %llu prime?\n", cur);
    if (IsPrimeInternal(cur)) {
      // printf("Write %llu @%d\n", cur, nf);
      factors[nf++] = cur;
    } else {
      // printf("So, rho...\n");
      FactorUsingPollardRho(cur, 1, factors, &nf, failed);
    }
  }

  // printf("set num_factors to %d\n", nf);
  *num_factors = nf;
}

__kernel void Factorize(__global const uint64_t *restrict nums,
                        __global uint64_t *restrict all_out,
                        __global uint8_t *restrict all_out_size) {

  const int idx = get_global_id(0);
  const uint64_t x = nums[idx];
  uint64_t *out = &all_out[idx * MAX_FACTORS];

  int num_factors = 0;
  bool failed = false;
  FactorizeInternal(x, out, &num_factors, &failed);
  if (failed) {
    all_out_size[idx] = 0xFF;
  } else {
    all_out_size[idx] = num_factors;
  }
}
