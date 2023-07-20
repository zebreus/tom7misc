// This is a GPU filter for "Try".
// We don't try to completely replicate the function here; the
// goal is to quickly throw out the many cases that have no
// interesting squares.
//
// Specifically, we look inputs where there is no way of
// taking three pairs such that
//
//  [a]  B   C
//
//   D   E  [f]
//
//   G  [h]  I
//
//
// where D^2 + E^2 = C^2 + I^2.

// Careful! The builtin uint8 is a vector of 8 uints.
typedef uchar uint8_t;
typedef uint uint32_t;
typedef ulong uint64_t;
typedef long int64_t;
typedef atomic_uint atomic_uint32_t;


// Immediately pass the filter if we find anything that satisfies
// this; otherwise just count.
#define TRY(/* a, */ bb_, cc, dd, ee, /* f, */ gg_, /* h, */ ii) \
  if (cc + ii == dd + ee) {     \
    rejected[sum_idx] = 0;      \
    return;                     \
  } else {                      \
    rej++;                      \
  }

// Same args as NWaysMerge: The sum, where sum = a^2 + b^2 for each
// of the ways_size/2 pairs in the "ways" row. In 'rejected', we
// write zero if there might be something in here (i.e. we should
// try the full routine on CPU) or the number of rejected combinations,
// which should be added to the rejected_f counter.
__kernel void TryFilter(__global const uint64_t *restrict sums,
                        __global const uint32_t *restrict ways_size,
                        __global const uint64_t *restrict ways,
                        __global uint32_t *restrict rejected) {
  const int sum_idx = get_global_id(0);
  const uint64_t sum = sums[sum_idx];
  const uint32_t ways_row_base = sum_idx * MAX_WAYS * 2;

  // N^3 loop over ways. Note that we can tune how deep we're
  // willing to go here, since we can always just return zero
  // and not filter.
  const uint32_t num_ways = ways_size[sum_idx] >> 1;

  uint32_t rej = 0;

  // PERF: Could premultiply the elements (separate kernel) like
  // we now do in the CPU version. (In fact we don't need
  // the roots at all for this filter, so we could definitely
  // simplify...)
  for (int p = 0; p < num_ways; p++) {
    const uint64_t b = ways[ways_row_base + p * 2 + 0];
    const uint64_t c = ways[ways_row_base + p * 2 + 1];
    const uint64_t bb = b * b;
    const uint64_t cc = c * c;

    for (int q = 0; q < num_ways; q++) {
      if (p != q) {
        const uint64_t d = ways[ways_row_base + q * 2 + 0];
        const uint64_t g = ways[ways_row_base + q * 2 + 1];
        const uint64_t dd = d * d;
        const uint64_t gg = g * g;

        // require that the smallest of b,c,d,g appears on the
        // top, to reduce symmetries.
        if (min(b, c) > min(d, g))
          continue;

        for (int r = 0; r < num_ways; r++) {
          if (p != r && q != r) {
            const uint64_t e = ways[ways_row_base + r * 2 + 0];
            const uint64_t i = ways[ways_row_base + r * 2 + 1];
            const uint64_t ee = e * e;
            const uint64_t ii = i * i;

            // Now eight ways of ordering the pairs.
            TRY(/**/ bb,  cc,
                dd,  ee,  /**/
                gg,  /**/ ii);
            TRY(/**/ cc,  bb,
                dd,  ee,  /**/
                gg,  /**/ ii);
            TRY(/**/ bb,  cc,
                gg,  ee,  /**/
                dd,  /**/ ii);
            TRY(/**/ cc,  bb,
                gg,  ee,  /**/
                dd,  /**/ ii);

            TRY(/**/ bb,  cc,
                dd,  ii,  /**/
                gg,  /**/ ee);
            TRY(/**/ cc,  bb,
                dd,  ii,  /**/
                gg,  /**/ ee);
            TRY(/**/ bb,  cc,
                gg,  ii,  /**/
                dd,  /**/ ee);
            TRY(/**/ cc,  bb,
                gg,  ii,  /**/
                dd,  /**/ ee);
          }
        }
      }
    }
  }

  // It gets filtered out, with this counter.
  rejected[sum_idx] = rej;
}
