// Returns the lower bound on the square root of xx.
// If xx = x^2, then this is x.

// Careful! The builtin uint8 is a vector of 8 uints.
typedef uchar uint8_t;
typedef uint uint32_t;
typedef ulong uint64_t;

// https://www.nuprl.org/MathLibrary/integer_sqrt/
static uint64_t Sqrt64(uint64_t xx) {
  if (xx <= 1) return xx;
  // z = xx / 4
  uint64_t z = xx >> 2;
  uint64_t r2 = 2 * Sqrt64(z);
  uint64_t r3 = r2 + 1;
  return (xx < r3 * r3) ? r2 : r3;
}

__kernel void SieveG(__global uint8_t *sieved) {

  /*
  a b c
  d e f
  g h i
  */

  // Fixed at compile-time.
  const uint32_t a = A;
  const uint32_t b = B;
  const uint32_t c = C;
  const uint64_t aa = (uint64_t)a * (uint64_t)a;
  const uint64_t bb = (uint64_t)b * (uint64_t)b;
  const uint64_t cc = (uint64_t)c * (uint64_t)c;
  // XXX should not allow overflow
  const uint64_t SUM = aa + bb + cc;

  // A value for any additional cell introduces a constraint
  // that may make it unsolvable, because e.g. a^2 + d^2 + g^2 = SUM,
  // but g may not be integral if we solve for it.

  // Output is a byte mask.
  uint32_t chunk = get_global_id(0);
  uint32_t dhi = chunk << 3;
  uint8_t out = 0;
  const uint64_t sum_minus_aa = SUM - aa;
  // PERF: Don't even call the kernel, then...
  if ((dhi * dhi) <= sum_minus_aa) {
    for (int x = 0; x < 8; x++) {
      out <<= 1;
      uint32_t d = dhi | x;
      uint64_t dd = (uint64_t)d * (uint64_t)d;
      // Does there exist g such that aa + dd + gg = SUM?
      uint64_t gg = sum_minus_aa - dd;
      uint64_t g = Sqrt64(gg);
      if (g * g == gg) {
        // printf("%llu * %llu = %llu\n", g, g, gg);
        out |= 1;
      }
    }
  }
  sieved[chunk] = out;
}

// So, we can do this same thing for g, h, i.
// (In fact we can use the same kernel. Just think of it as
// "column sieve" for some particular a, b, c).
//
// Then we have g, h, i that are feasible. Maybe the cardinality
// of that set is generally reasonable? Otherwise we can sieve
// again...
