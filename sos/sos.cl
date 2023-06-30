
// Careful! The builtin uint8 is a vector of 8 uints.
typedef uchar uint8_t;
typedef uint uint32_t;
typedef ulong uint64_t;

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

// Returns the lower bound on the square root of xx.
// If xx = x^2, then this is x.
// TODO: double version ok on GPU?
// https://www.nuprl.org/MathLibrary/integer_sqrt/
static uint64_t Sqrt64Nuprl(uint64_t xx) {
  if (xx <= 1) return xx;
  // z = xx / 4
  uint64_t z = xx >> 2;
  uint64_t r2 = 2 * Sqrt64Nuprl(z);
  uint64_t r3 = r2 + 1;
  return (xx < r3 * r3) ? r2 : r3;
}

inline static uint64_t Sqrt64(uint64_t n) {
  uint64_t r = (uint64_t)sqrt((double)n);
  return r - (r * r >= n + 1);
}

// Try to find b such that a^2 + b^2 = sum, with a <= b.
// If successful, output (a,b) to out[out_size] and increment
// out_size by two.
__kernel void NWays(__global const uint64_t *restrict sums,
                    __global uint32_t *restrict out_size,
                    __global uint64_t *restrict out) {


  // The way we ensure distinctness is that the pairs are ordered
  // a < b, and the search (and vector) is ordered by the first
  // element.

  const uint64_t a = get_global_id(0);
  const int sum_idx = get_global_id(1);
  const uint64_t sum = sums[sum_idx];

  const uint64_t aa = a * a;

  const uint64_t target = sum - aa;
  if (!MaybeSquare(target))
    return;

  // The limit is set for the largest sum, so we need to deal with
  // this raggedness. I think the advantage of testing early could be
  // that a batch of work units finish early (when we reach the
  // padding part). The advantage of testing late is that threads
  // would reach the most common bail-out (MaybeSquare) with fewer
  // instructions. This latter thing seems better?
  if (aa * 2 > sum)
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
  // now test (r-1)^2, but without multiplying
  // consider r^2 - 2r + 1 as (r-1)^?
  if (rr - (r << 1) + 1 == target) {
    r--;
    goto found;
  }

  // Not square.
  return;

 found:;

  const uint64_t b = r;

  // Insist that the result is smaller than the
  // input, even if it would work. We find it the
  // other way. Try x = 7072 for sum = 100012225.
  if (b < a)
    return;

  // Get unique indices in this row's output array. This is very rare
  // so the synchronization overhead should not be too bad.
  uint32_t row_idx = atomic_add(&out_size[sum_idx], 2);
  const uint32_t row_base = sum_idx * MAX_WAYS * 2;
  out[row_base + row_idx] = a;
  out[row_base + row_idx + 1] = b;
}
