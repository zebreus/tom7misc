
#include "hash-util.h"

#include <array>

#include "choppy.h"
#include "expression.h"
#include "base/logging.h"
#include "ansi.h"

using Choppy = ChoppyGrid<256>;
using Allocator = Exp::Allocator;
using DB = Choppy::DB;

using namespace half_float::literal;

std::array<const Exp *, 8> HashUtil::PermuteFn(
    const std::array<int, 64> &perm,
    DB *basis, int x) {
  Allocator *alloc = &basis->alloc;
  // Position of the first (most significant) bit in this byte.
  const int start_pos = x * 8;
  // Positions of the input bits (from the whole 64-bit word),
  // left to right.
  std::array<int, 8> bits;
  bits.fill(-1);
  CHECK(perm.size() == 64);
  for (int i = 0; i < perm.size(); i++) {
    int idx = perm[i];
    if (idx >= start_pos && idx < start_pos + 8) {
      // then this bit outputs into the target byte
      bits[idx - start_pos] = i;
    }
  }
  for (int x : bits) CHECK(x >= 0) << "not a permutation?";

  #if 0
  printf("Source bits for byte " AGREEN("%d") ":", x);
  for (int x : bits) printf(" %d", x);
  printf("\n");
  #endif

  // Now compute the function for each byte.
  std::array<const Exp *, 8> fs;
  for (int f = 0; f < 8; f++) {
    const int inbyte_start_pos = f * 8;
    std::vector<const Exp *> parts;
    for (int b = 0; b < 8; b++) {
      int bit = bits[b];
      const uint8_t out_mask = 1 << (7 - b);
      if (bit >= inbyte_start_pos && bit < inbyte_start_pos + 8) {
        // Bit is from this byte.
        const int inbyte_offset = bit - inbyte_start_pos;
        // Mask for that one bit.
        const uint8_t mask = 1 << (7 - inbyte_offset);
        // Now we want an indicator function that tells us if
        // this bit is set. The function's value is 1 grid cell
        // if so, else 0.

        // 128 expressions!
        // PERF some of these are going to have much simpler
        // expressions.
        std::vector<const Exp *> bitselect_parts;
        for (int z = 0; z < 256; z++) {
          if (z & mask) {
            // bit is set for this byte value
            DB::key_type key = DB::BasisKey(z);
            auto it = basis->fns.find(key);
            CHECK(it != basis->fns.end()) << "Incomplete basis: "
                                          << DB::KeyString(key);
            bitselect_parts.push_back(it->second);
          }
        }

        // The indicator function sums them all.
        CHECK(!bitselect_parts.empty());
        const Exp *indicator = bitselect_parts[0];
        for (int i = 1; i < bitselect_parts.size(); i++) {
          indicator = alloc->PlusE(indicator, bitselect_parts[i]);
        }

        // The indicator produces one grid cell (or zero), so
        // first make it produce 1.0

        indicator = alloc->TimesC(
            indicator,
            Exp::GetU16((half)(Choppy::GRID / 2.0)));

        #if 0
        printf("[%d] f %d Bit %d (source %d) outmask %02x "
               "(inbyte %d mask %02x)\n",
               x, f, b, bit, out_mask, inbyte_offset, mask);
        #endif

        // Then multiply by the power of two.
        indicator = alloc->TimesC(
            indicator,
            Exp::GetU16((half)out_mask));

#if 0
        // XXX ded.
        half hh = (half)(out_mask / (Choppy::GRID / 2.0));
        printf("out mask is %02x. so scale %.6f\n", out_mask, (float)hh);

        // Then we want it to take on the value in out_mask,
        // so that it contributes the corresponding bit. Note that
        // this
        indicator = alloc->TimesC(
            indicator,
            Exp::GetU16((half)(out_mask / (Choppy::GRID / 2.0))));
#endif

        parts.push_back(indicator);
      }
    }

    if (parts.empty()) {
      // This can happen if the out byte doesn't depend on
      // anything in the input byte.
      fs[f] = alloc->TimesC(alloc->Var(), 0);
    } else {
      const Exp *sum = parts[0];
      for (int i = 1; i < parts.size(); i++) {
        sum = alloc->PlusE(sum, parts[i]);
      }
      fs[f] = sum;
    }
  }
  return fs;
}
