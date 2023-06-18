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

// Start with a sum of squares.
__kernel void SieveDoubleSums(__global uint8_t *sieved) {
  // Output is a byte mask.
  uint32_t chunk = get_global_id(0);
  uint64_t sum = (uint64_t)SUM_HI |
  uint8_t out = 0;
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
