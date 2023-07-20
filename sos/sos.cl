
// New version, based on nsoks.

#define SUM_STRIDE 3

// Careful! The builtin uint8 is a vector of 8 uints.
typedef uchar uint8_t;
typedef uint uint32_t;
typedef ulong uint64_t;
typedef long int64_t;
typedef atomic_uint atomic_uint32_t;

/* MAGIC[N] has a bit i set iff i is a quadratic residue mod N.  */
# define MAGIC64 0x0202021202030213ULL
# define MAGIC63 0x0402483012450293ULL
# define MAGIC65 0x218a019866014613ULL
# define MAGIC11 0x23b

/* Return the square root if the input is a square, otherwise 0.  */
inline static bool
MaybeSquare(uint64_t x) {
  /* Uses the tests suggested by Cohen.  Excludes 99% of the non-squares before
     computing the square root.  */
  return (((MAGIC64 >> (x & 63)) & 1)
          && ((MAGIC63 >> (x % 63)) & 1)
          /* Both 0 and 64 are squares mod (65).  */
          && ((MAGIC65 >> ((x % 65) & 63)) & 1)
          && ((MAGIC11 >> (x % 11) & 1)));
}

inline static uint64_t Sqrt64(uint64_t n) {
  uint64_t r = (uint64_t)sqrt((double)n);
  return r - (r * r >= n + 1);
}

// First, handle the case of perfect squares (a^2 + 0^2 = a^2). 1D
// kernel; also initializes the out_size (to 0 or 2).
__kernel void PerfectSquares(__global const uint64_t *restrict sums,
                             __global atomic_uint32_t *restrict out_size,
                             __global uint64_t *restrict out) {
  const int sum_idx = get_global_id(0);
  const uint64_t sum = sums[sum_idx * SUM_STRIDE + 0];
  // Is there any point to this test? I guess it could help for batches
  // that have no squares?
  if (!MaybeSquare(sum))
    goto not_found;

  uint64_t r = (uint64_t)sqrt((double)sum);
  // now either r or r-1 is the square root.
  const uint64_t rr = r * r;
  if (rr == sum) goto found;
  // now test (r-1)^2 (without multiplying) as r^2 - 2r + 1.
  if (rr - (r << 1) + 1 == sum) {
    r--;
    goto found;
  }

 not_found:;
  // Not square.
  // out_size[sum_idx] = 0;
  atomic_init(&out_size[sum_idx], 0);
  return;

 found:;
  // No need for synchronization here because we only run one test
  // per sum.
  // out_size[sum_idx] = 2;
  atomic_init(&out_size[sum_idx], 2);
  const uint32_t row_base = sum_idx * MAX_WAYS * 2;
  out[row_base + 0] = 0;
  out[row_base + 1] = r;
}

// Try to find b such that a^2 + b^2 = sum, with a <= b.
// If successful, output (a,b) to out[out_size] and increment
// out_size by two.
__kernel void NWays(uint64_t base_trialsquare,
                    __global const uint64_t *restrict sums,
                    __global atomic_uint32_t *restrict out_size,
                    __global uint64_t *restrict out) {
  #if TRANSPOSE
  #define IN_X 1
  #define IN_Y 0
  #else
  #define IN_X 0
  #define IN_Y 1
  #endif
  const int sum_idx = get_global_id(IN_Y);
  const uint64_t sum = sums[SUM_STRIDE * sum_idx + 0];

  const uint64_t trialsquare = base_trialsquare + get_global_id(IN_X);

  const uint64_t aa = trialsquare * trialsquare;
  const uint64_t target = sum - aa;

  if (!MaybeSquare(target))
    return;

  // The following block is equivalent to this:
  //
  // const uint64_t b = Sqrt64(target);
  // if (b * b != target)
  //   return;
  //
  // but avoids some duplicate work. Speeds up the kernel by about 2%.

  uint64_t r = (uint64_t)sqrt((double)target);
  // now either r or r-1 is the square root.
  const uint64_t rr = r * r;
  if (rr == target) goto found;
  // now test (r-1)^2 as r^2 - 2r + 1.
  if (rr - (r << 1) + 1 == target) {
    r--;
    goto found;
  }

  // Not square.
  return;

 found:;

  // Deal with raggedness. We would only have run this code if
  // mn <= trialsquare <= mx.
  const uint64_t mn = sums[SUM_STRIDE * sum_idx + 1];
  const uint64_t mx = sums[SUM_STRIDE * sum_idx + 2];

  if (trialsquare < mn || trialsquare > mx) return;

  // Get unique indices in this row's output array. This is very rare
  // so the synchronization overhead should not be too bad.
  // XXX fix opencl warning; this wants "volatile int *"?
  const uint32_t row_idx = atomic_add(&out_size[sum_idx], 2);
  const uint32_t row_base = sum_idx * MAX_WAYS * 2;
  out[row_base + row_idx] = trialsquare;
  out[row_base + row_idx + 1] = r;
}


// Same output behavior as above; different approach.
__kernel void NWaysMerge(__global const uint64_t *restrict sums,
                         __global uint32_t *restrict out_size,
                         __global uint64_t *restrict out) {
  const int sum_idx = get_global_id(0);
  const uint64_t sum = sums[sum_idx];
  const uint32_t out_row_base = sum_idx * MAX_WAYS * 2;

  // PERF: Or could track num_ways *2 ?
  uint64_t num_ways = 0;

  // PERF: Might be faster to do on CPU during prep?
  uint64_t root = Sqrt64(sum);

  uint64_t a = 0;
  uint64_t aa = 0;
  uint64_t b = root + 1;
  uint64_t bb = b * b;

  // uint64_t aaplusbb = aa + bb;
  // We track the signed error vs the sum, rather than a^2 + b^2 itself.
  int64_t aaplusbbminussum = (int64_t)(aa + bb) - (int64_t)sum;

  while (b >= a) {
    if (aaplusbbminussum == 0) {
      // Don't need atomic operations in this one. We are the only
      // writer to this region.
      const uint32_t idx = out_row_base + (num_ways * 2);
      out[idx] = a;
      out[idx + 1] = b;
      num_ways++;
    }

    // uint64_t ap = a + 1;
    // (a + 1) * (a + 1) == a^2 + 2a + 1
    // uint64_t apap = aa + a + ap;
    // uint64_t apap = aa + (a << 1) + 1;
    // this is the term that when added to a^2, gives us (a+1)^2
    uint64_t ainc = (a << 1) + 1;

    // uint64_t bm = b - 1;
    // (b - 1) * (b - 1) == b^2 - 2b + 1
    // uint64_t bmbm = bb - (b << 1) + 1;
    // this is the term that when subtracted from b^2, gives (b-1)^2
    uint64_t bdec = (b << 1) - 1;

    // Either increase a or decrease b. Which one gets
    // us closer to sum?
    // uint64_t asum = apap + bb;
    // uint64_t asum = aaplusbb + ainc;
    // uint64_t bsum = aa + bmbm;
    // uint64_t bsum = aaplusbb - bdec;

    int64_t aerr = aaplusbbminussum + ainc;
    int64_t berr = aaplusbbminussum - bdec;
    // PERF: Maybe we could do this with abs_diff?

    if (abs(aerr) < abs(berr)) {
      a++;
      // aa += ainc;
      aaplusbbminussum = aerr;
    } else {
      b--;
      // bb -= bdec;
      aaplusbbminussum = berr;
    }
  }

  out_size[sum_idx] = num_ways * 2;
}

