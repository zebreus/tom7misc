
// Careful! The builtin uint8 is a vector of 8 uints.
typedef uchar uint8_t;
typedef uint uint32_t;
typedef ulong uint64_t;
typedef long int64_t;
typedef short int16_t;
typedef atomic_uint atomic_uint32_t;

// Some pieces of this code were ported from cc-lib factorize.cc,
// which was in turn from gnu's factor utility.

// Note: We have hand-written PTX for sme of these in factorize.cl,
// but they didn't benchmark faster for me. :( Keeping this copy simple..

// Subtracts 128-bit words.
// returns high, low
inline ulong2
Sub128(uint64_t ah, uint64_t al,
       uint64_t bh, uint64_t bl) {
  uint64_t carry = al < bl;
  ulong2 ret;
  ret.s0 = ah - bh - carry;
  ret.s1 = al - bl;
  return ret;
}

// Right-shifts a 128-bit quantity by count.
// returns high, low
inline ulong2
RightShift128(uint64_t ah, uint64_t al, int count) {
  // asm("/* Right Shift 128 */");
  ulong2 ret;
  ret.s0 = ah >> count;
  ret.s1 = (ah << (64 - count)) | (al >> count);
  // asm("/* End Right Shift 128 */");
  return ret;
}

// This is maybe slightly shorter than the C code, but the
// real benefit of doing this with subtraction is that we
// immediately do the same 128-bit subtraction in UDiv128Rem,
// so we end up reusing the calculation.
inline bool GreaterEq128(uint64_t ah, uint64_t al,
                         uint64_t bh, uint64_t bl) {
#define PTX_GEQ128 1
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

// returns (w1, w0)
inline ulong2 UMul128(uint64_t u, uint64_t v) {
#define PTX_MUL128 1
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

typedef uint64_t Montgomery64;

#define MontgomeryZero 0

struct MontgomeryRep64 {
  uint64_t modulus;
  uint64_t inv;
  // 2^64 mod modulus, which is the representation of 1.
  Montgomery64 r;
  // (2^64)^2 mod modulus.
  // uint64_t r_squared;
};

inline void Represent(uint64_t modulus, struct MontgomeryRep64 *rep) {
  rep->modulus = modulus;
  rep->r = Redcify(1, modulus);
  rep->inv = Binv(modulus);
  // PERF: might want to compute r^2.
}

#define MontgomeryOne(rep) ((rep).r)

// Assumes x < modulus
inline Montgomery64 ToMontgomery(uint64_t x, struct MontgomeryRep64 *rep) {
  return Redcify(x, rep->modulus);
}

inline Montgomery64 MontgomerySub(Montgomery64 a, Montgomery64 b,
                                  struct MontgomeryRep64 *rep) {
  return SubMod(a, b, rep->modulus);
}

inline Montgomery64 MontgomeryAdd(Montgomery64 a, Montgomery64 b,
                                  struct MontgomeryRep64 *rep) {
  return AddMod(a, b, rep->modulus);
}

inline Montgomery64 MontgomeryMult(Montgomery64 a, Montgomery64 b,
                                  struct MontgomeryRep64 *rep) {

  return MulRedc(a, b, rep->modulus, rep->inv);
}

inline Montgomery64 MontgomeryPow(Montgomery64 base, uint64_t exponent,
                                  struct MontgomeryRep64 *rep) {
  return PowM(base, exponent, rep->modulus, rep->inv, rep->r);
}

bool DoOneQuick(uint64_t prime_int, int64_t m_int, int64_t n_int) {
  // PERF: Some of this can be done once up front for each p.
  struct MontgomeryRep64 rep;
  Represent(prime_int, &rep);

  const Montgomery64 coeff_1 = ToMontgomery(222121, &rep);
  const Montgomery64 coeff_2 = ToMontgomery(360721, &rep);

  // These must be non-negative.
  if (m_int < 0) m_int += rep.modulus;
  if (n_int < 0) n_int += rep.modulus;

  const Montgomery64 m = ToMontgomery((uint64_t)m_int, &rep);
  const Montgomery64 n = ToMontgomery((uint64_t)n_int, &rep);

  // Sub is a little faster, so pre-negate these.
  const Montgomery64 neg_m = MontgomerySub(MontgomeryZero, m, &rep);
  const Montgomery64 neg_n = MontgomerySub(MontgomeryZero, n, &rep);

  // Try a few candidates.
  // PERF: Tune the limit here.
  for (uint64_t idx = 0; idx < QUICK_PASS_SIZE; idx++) {
    // Get one of the residues. We don't care which ones we
    // try or in what order. Since the montgomery representatives
    // are a permutation of the numbers [0, p), we can just use
    // the index itself.
    Montgomery64 a = idx;
    Montgomery64 aa = MontgomeryMult(a, a, &rep);

    // 222121 a^2 - (-m) = b^2
    // 360721 a^2 - (-n) = c^2

    const Montgomery64 t1 = MontgomeryMult(coeff_1, aa, &rep);
    const Montgomery64 t2 = MontgomeryMult(coeff_2, aa, &rep);

    Montgomery64 a1m = MontgomerySub(t1, neg_m, &rep);
    Montgomery64 a2n = MontgomerySub(t2, neg_n, &rep);

    // Compute Euler criteria. a^((p-1) / 2) must be 1.
    // Since p is odd, we can just shift down by one
    // to compute (p - 1)/2.
    uint64_t exponent = rep.modulus >> 1;
    Montgomery64 r1 = MontgomeryPow(a1m, exponent, &rep);
    Montgomery64 r2 = MontgomeryPow(a2n, exponent, &rep);

    bool sol1 = r1 == MontgomeryOne(rep) || a1m == MontgomeryZero;
    bool sol2 = r2 == MontgomeryOne(rep) || a2n == MontgomeryZero;

    if (sol1 && sol2) {
      return true;
    }
  }

  return false;
}

__kernel void InitializeAtomic(// Current count in outs. size: height
                               __global atomic_uint32_t *restrict out_size) {
  const int mn_idx = get_global_id(0);
  atomic_init(&out_size[mn_idx], 0);
}

__kernel void QuickPass(// Input primes. size: width
                        __global const uint64_t *restrict primes,
                        // Input (m,n) pairs. size: height * 2
                        __global const int16_t *restrict mns,
                        // Output primes per (m, n) that need full
                        // pass. size: height * MAX_FULL_RUNS
                        __global uint64_t *restrict out,
                        // Current count in outs. size: height
                        __global atomic_uint32_t *restrict out_size) {

  const int prime_idx = get_global_id(0);
  const int mn_idx = get_global_id(1);

  const uint64_t prime = primes[prime_idx];
  const int m = mns[mn_idx * 2 + 0];
  const int n = mns[mn_idx * 2 + 1];

  if (DoOneQuick(prime, m, n)) {
    // Found a solution, so it passes the filter.
  } else {
    // Rare.

    // Claim a slot. Note that the array might be full, but we can't
    // check that separately because the (negative) check is out of
    // date as soon as the atomic operation finishes.
    const uint32_t col_idx = atomic_add(&out_size[mn_idx], 1);

    // ... but only actually do the write if we're still in bounds.
    if (col_idx < MAX_FULL_RUNS) {
      out[mn_idx * MAX_FULL_RUNS + col_idx] = prime;
    }
  }
}
