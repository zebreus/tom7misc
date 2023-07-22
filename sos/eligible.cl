
typedef uchar uint8_t;
typedef uint uint32_t;
typedef ulong uint64_t;
typedef long int64_t;
typedef atomic_uint atomic_uint32_t;

// Writes bitmask to out.
__kernel void NotSumOfSquares(const uint64_t start,
                              __global uint8_t *restrict out) {
  const int idx = get_global_id(0);
  const uint64_t num_base = start + (idx * 8);

  // We set the bit if we know that it cannot be a sum of
  // squares.
  uint8_t byte = 0;
  // force unroll?
  for (int i = 0; i < 8; i++) {
    uint64_t sum = num_base + i;

    uint8_t bit =
      // non-quadratic sum residues.
      (((0x0000000001209048LLU >> (sum % 27)) & 1) |
       ((0x0000040810204080LLU >> (sum % 49)) & 1) |
       ((0xd9c9d8c8d9c8d8c8LLU >> (sum % 64)) & 1));

    // This is probably overkill for the GPU filter!
    // I haven't checked these for redundancy, either.

    // 10/10
    if ((23 * (sum % 121)) % 127 > 115) bit = 1;
    // 18/18
    if ((170 * (sum % 361)) % 359 > 340) bit = 1;
    // 22/22
    if ((47 * (sum % 529)) % 541 > 517) bit = 1;
    // 30/30
    if ((63 * (sum % 961)) % 977 > 945) bit = 1;
    // 42/42
    if ((87 * (sum % 1849)) % 1871 > 1827) bit = 1;
    // 46/46
    if ((189 * (sum % 2209)) % 2221 > 2173) bit = 1;
    // 58/58
    if ((119 * (sum % 3481)) % 3511 > 3451) bit = 1;
    // 66/66
    if ((135 * (sum % 4489)) % 4523 > 4455) bit = 1;
    // 70/70
    if ((143 * (sum % 5041)) % 5077 > 5005) bit = 1;
    // 78/78
    if ((396 * (sum % 6241)) % 6257 > 6177) bit = 1;
    // 82/82
    if ((250 * (sum % 6889)) % 6917 > 6833) bit = 1;
    // 102/102
    if ((2679 * (sum % 10609)) % 10613 > 10509) bit = 1;
    // 106/106
    if ((643 * (sum % 11449)) % 11467 > 11359) bit = 1;
    // 126/126
    if ((380 * (sum % 16129)) % 16087 > 15960) bit = 1;
    // 130/130
    if ((1561 * (sum % 17161)) % 17041 > 16910) bit = 1;
    // 262/262
    if ((6289 * (sum % 69169)) % 68917 > 68654) bit = 1;
    // 210/210
    if ((1892 * (sum % 44521)) % 44357 > 44146) bit = 1;
    // 198/198
    if ((11288 * (sum % 39601)) % 39409 > 39210) bit = 1;
    // 310/310
    if ((20467 * (sum % 96721)) % 96443 > 96132) bit = 1;
    // 222/222
    if ((7997 * (sum % 49729)) % 49537 > 49314) bit = 1;
    // 226/226
    if ((20568 * (sum % 51529)) % 51307 > 51080) bit = 1;
    // 190/190
    if ((12161 * (sum % 36481)) % 36293 > 36102) bit = 1;
    // 162/162
    if ((162 * (sum % 26569)) % 26407 > 26244) bit = 1;
    // 178/178
    if ((713 * (sum % 32041)) % 31907 > 31728) bit = 1;
    // 306/306
    if ((13465 * (sum % 94249)) % 93949 > 93642) bit = 1;
    // 282/282
    if ((13876 * (sum % 80089)) % 80141 > 79857) bit = 1;
    // 250/250
    if ((2260 * (sum % 63001)) % 63029 > 62777) bit = 1;
    // 166/166
    if ((1003 * (sum % 27889)) % 27917 > 27749) bit = 1;
    // 330/330
    if ((15231 * (sum % 109561)) % 109597 > 109265) bit = 1;
    // 138/138
    if ((1673 * (sum % 19321)) % 19379 > 19239) bit = 1;
    // 150/150
    if ((452 * (sum % 22801)) % 22751 > 22600) bit = 1;
    // 238/238
    if ((4062 * (sum % 57121)) % 57107 > 56868) bit = 1;
    // 270/270
    if ((1354 * (sum % 73441)) % 73387 > 73116) bit = 1;

    byte |= bit << (7 - i);
  }

  out[idx] = byte;
}
