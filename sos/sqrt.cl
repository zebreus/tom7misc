
typedef uint uint32_t;
typedef ulong uint64_t;
typedef int int32_t;
typedef long int64_t;

inline float uint32_as_float(uint32_t u) {
  return as_float(u);
}

inline uint32_t float_as_uint32(float f) {
  return as_uint(f);
}

// OpenCL should by default use round-to-nearest.
inline float int2float_rn(int32_t i) {
  return (float)i;
}

inline int32_t float2int_rn(float f) {
  int32_t s1;
  asm("cvt.rni.s32.f32 %0, %1;" : "=r"(s1) : "f"(f));
  return s1;
}

// This is from github.com/SChernykh/fast_int_math_v2_cuda (GPL)
// and it produces almost identical PTX, but it doesn't work
// (not even close!) what gives?
inline uint32_t fast_sqrt_v2(const uint64_t n1) {
  float x = uint32_as_float((((uint32_t*)&n1)[1] >> 9) + ((64U + 127U) << 23));
  float x1;
  asm("rsqrt.approx.f32 %0, %1;" : "=f"(x1) : "f"(x));
  asm("sqrt.approx.f32 %0, %1;" : "=f"(x) : "f"(x));

  // The following line does x1 *= 4294967296.0f;
  x1 = uint32_as_float(float_as_uint32(x1) + (32U << 23));

  const uint32_t x0 = float_as_uint32(x) - (158U << 23);
  const int64_t delta0 = n1 - (((int64_t)(x0) * x0) << 18);
  const float delta = int2float_rn(((int32_t*)&delta0)[1]) * x1;

  uint32_t result = (x0 << 10) + float2int_rn(delta);
  const uint32_t s = result >> 1;
  const uint32_t b = result & 1;

  const uint64_t x2 = (uint64_t)(s) * (s + b) + ((uint64_t)(result) << 32) - n1;
  if ((int64_t)(x2 + b) > 0) --result;
  if ((int64_t)(x2 + 0x100000000UL + s) < 0) ++result;

  return result;
}


inline static uint64_t Sqrt64(uint64_t n) {
  uint64_t r = (uint64_t)sqrt((double)n);
  return r - (r * r >= n + 1);
}

/* by Mark Crowne */
uint32_t MCrowne(uint64_t val) {
  uint64_t temp, g=0;

  if (val >= 0x40000000) {
    g = 0x8000;
    val -= 0x40000000;
  }

#define INNER_ISQRT(s)                             \
  temp = (g << (s)) + (1ULL << ((s) * 2 - 2));     \
  if (val >= temp) {                               \
    g += 1ULL << ((s)-1);                          \
    val -= temp;                                   \
  }

  INNER_ISQRT (15)
  INNER_ISQRT (14)
  INNER_ISQRT (13)
  INNER_ISQRT (12)
  INNER_ISQRT (11)
  INNER_ISQRT (10)
  INNER_ISQRT ( 9)
  INNER_ISQRT ( 8)
  INNER_ISQRT ( 7)
  INNER_ISQRT ( 6)
  INNER_ISQRT ( 5)
  INNER_ISQRT ( 4)
  INNER_ISQRT ( 3)
  INNER_ISQRT ( 2)

#undef INNER_ISQRT

  temp = g+g+1;
  if (val >= temp) g++;
  return g;
}

/* by Jim Ulery */
static uint64_t Julery(uint64_t val) {
  uint64_t temp, g=0, b = 0x80000000ULL, bshft = 31;
  do {
    if (val >= (temp = (((g << 1) + b)<<bshft--))) {
      g += b;
      val -= temp;
    }
  } while (b >>= 1);
  return g;
}

__kernel void SquareRoot(__global const uint64_t *restrict input,
                         __global uint64_t *restrict output) {
  const int idx = get_global_id(0);
  uint64_t rr = input[idx];

  // uint64_t r = Sqrt64(rr);
  uint64_t r = Julery(rr);
  // uint64_t r = fast_sqrt_v2(rr);
  output[idx] = r;
}
