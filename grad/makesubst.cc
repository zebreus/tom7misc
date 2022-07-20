
#include <optional>
#include <array>
#include <vector>
#include <set>
#include <string>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "expression.h"
#include "half.h"
#include "hashing.h"

#include "choppy.h"
#include "grad-util.h"
#include "color-util.h"
#include "arcfour.h"
#include "ansi.h"
#include "timer.h"

using DB = Choppy::DB;
using Allocator = Exp::Allocator;
using Table = Exp::Table;

static std::optional<std::array<int, Choppy::GRID>>
VerboseChoppy(const Exp *exp, ImageRGBA *img) {
  static constexpr int GRID = Choppy::GRID;
  static constexpr double EPSILON = Choppy::EPSILON;

  CHECK(img->Width() == img->Height());
  const int size = img->Width();

  auto MapCoord = [size](double x, double y) -> pair<int, int> {
    int xs = (int)std::round((size / 2) + x * (size / 2));
    int ys = (int)std::round((size / 2) + -y * (size / 2));
    return make_pair(xs, ys);
  };

  auto DrawLine = [img, &MapCoord](double x0, double y0,
                                   double x1, double y1,
                                   uint32_t color) {
      auto [i0, j0] = MapCoord(x0, y0);
      auto [i1, j1] = MapCoord(x1, y1);
      img->BlendLine32(i0, j0, i1, j1, color);
    };

  std::array<int, GRID> ret;
  std::array<uint16_t, GRID> val;

  // Midpoints have to be integers.
  for (int i = 0; i < GRID; i++) {
    half x = (half)((i / (double)(GRID/2)) - 1.0);
    x += (half)(1.0/(GRID * 2.0));

    half y = Exp::GetHalf(Exp::EvaluateOn(exp, Exp::GetU16(x)));
    double yi = ((double)y + 1.0) * (GRID / 2);

    int yy = std::round(yi);
    if (fabs(yi - yy) > EPSILON) {
      // Not "integral."
      /*
        printf("Not integral at x=%.4f (y=%.4f)\n",
        (float)x, (float)y);
      */
      return {};
    }

    ret[i] = yy - (GRID / 2);
    val[i] = Exp::GetU16(y);
  }

  // Also check that the surrounding values are exactly equal.
  for (int i = 0; i < GRID; i++) {
    half x = (half)((i / (double)(GRID/2)) - 1.0);

    half low  = x + (half)(1 / (float)(GRID/2)) * (half)0.0125;
    half high = x + (half)(1 / (float)(GRID/2)) * (half)0.9975;

    {
      DrawLine(low, -0.01, low, +0.01, 0xFFFFFF44);
      DrawLine(low, 0.005, high, 0.005, 0xFFFFAA44);
      DrawLine(high, -0.01, high, +0.01, 0xFFFF7744);
    }

    /*
      printf("%d. x=%.3f check %.3f to %.3f\n",
      i, (float)x, (float)low, (float)high);
    */

    for (uint16 upos = Exp::GetU16(low);
         upos != Exp::GetU16(high);
         upos = Exp::NextAfter16(upos)) {
      uint16 v = Exp::EvaluateOn(exp, upos);
      if (val[i] != v && !((v & 0x7FFF) == 0 &&
                           (val[i] & 0x7FFF) == 0)) {
        // Not the same value for the interval.
        // (Maybe we could accept it if "really close"?)

        /*
          printf("%d. %.3f to %.3f. now %.4f=%04x. got %04x, had %04x\n",
          i, (float)low, (float)high, (float)pos,
          Exp::GetU16(pos), v, val[i]);
        */
        return {};
      }
    }
  }
  return ret;
}

static int NumWithAvalanche(const std::vector<uint8_t> &perm) {
  CHECK(std::has_single_bit(perm.size())) <<
    "mask must be a power of two";
  int power = std::countr_zero(perm.size());
  CHECK((1 << power) == perm.size());

  // Half of the bits.
  const int avalanche_target = power >> 1;

  int avalanche_times = 0;

  for (int idx = 0; idx < perm.size(); idx++) {
    const uint32_t value = perm[idx];
    for (int bit = 0; bit < power; bit++) {
      // Flip the one bit.
      const int oidx = idx ^ (1 << bit);
      CHECK(oidx >= 0 && oidx < (int)perm.size()) <<
        idx << " " << bit << " " << oidx;
      const uint32_t ovalue = perm[oidx];

      const uint32_t diff = value ^ ovalue;
      const int diffsize = std::popcount<uint32_t>(diff);
      // printf("%d want %d\n", diffsize, avalanche_target);
      if (diffsize >= avalanche_target)
        avalanche_times++;
    }
  }

  return avalanche_times;
}

static void RandStats() {
  ArcFour rc("randstats");
  std::vector<uint8_t> sbox;
  for (int i = 0; i < 256; i++) sbox.push_back(i);

  std::map<int, int> histo;

  const int count = 1000000;
  for (int i = 0; i < count; i++) {
    Shuffle(&rc, &sbox);

    histo[NumWithAvalanche(sbox)]++;
  }

  int cumul = 0;
  for (const auto &[k, v] : histo) {
    cumul += v;
    double pct = (cumul * 100.0) / count;
    printf("%04d avalanche: %d times (cumul. %.2f%%)\n", k, v, pct);
  }
}

static void AESStats() {
  // AES values.
  std::vector<uint8_t> sbox = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
    0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
    0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
    0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
    0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
    0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
    0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
    0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
    0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
    0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
    0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
  };

  // For comparison, a random one.
  {
    ArcFour rc(StringPrintf("aes.%lld", time(nullptr)));
    Shuffle(&rc, &sbox);
  }

  int visited[256] = {};

  for (int start_idx = 0; start_idx < 256; start_idx++) {
    if (!visited[start_idx]) {
      int idx = start_idx;
      int len = 0;
      do {
        visited[idx] = 1;
        len++;
        idx = sbox[idx];
      } while (idx != start_idx);
      printf("Cycle starting at %d, length: %d\n", start_idx, len);
    }
  }

  const size_t SIZE = 256;
  CHECK(std::has_single_bit(SIZE)) <<
    "mask must be a power of two";
  int power = std::countr_zero(SIZE);
  CHECK((1 << power) == SIZE);

  // Half of the bits.
  // const int avalanche_target = power >> 1;

  int bits[8] = {};

  for (int idx = 0; idx < SIZE; idx++) {
    const uint32_t value = sbox[idx];
    for (int bit = 0; bit < power; bit++) {
      // Flip the one bit.
      const int oidx = idx ^ (1 << bit);
      CHECK(oidx >= 0 && oidx < (int)SIZE) <<
        idx << " " << bit << " " << oidx;
      const uint32_t ovalue = sbox[oidx];

      const uint32_t diff = value ^ ovalue;
      const int diffsize = std::popcount<uint32_t>(diff);
      // printf("%d want %d\n", diffsize, avalanche_target);
      // if (diffsize != avalanche_target) goto again;
      bits[diffsize]++;
    }
  }

  for (int i = 0; i < 8; i++) {
    printf("diff %d bits: %d times\n", i, bits[i]);
  }

}


static int CycleLengthAt(const std::vector<int> &v,
                         int start_idx) {
  int len = 0, idx = start_idx;
  do {
    len++;
    idx = v[idx];
    CHECK(idx >= 0 && idx < v.size());
  } while (idx != start_idx);
  return len;
}

// Makes a permutation of the given size, which has the avalanche
// property (exactly).
[[maybe_unused]]
static std::vector<int> MakeAvalancheSubstBuggyDeleteme(
    ArcFour *rc,
    size_t size) {

  CHECK(std::has_single_bit(size)) <<
    "size must be a power of two";
  int power = std::countr_zero(size);
  CHECK((1 << power) == size);

  const int avalanche_target = power >> 1;

  std::vector<int> q;
  q.reserve(size);

  std::vector<int> ret;
  ret.reserve(size);

  for (;;) {
    ret.clear();

    // Start with the bytes in random order.
    q.clear();
    for (int i = 0; i < size; i++) q.push_back(i);
    Shuffle(rc, &q);

    for (int idx = 0; idx < size; idx++) {
      bool found_idx = false;
      // Pick the next byte that would produce the avalanche
      // property exactly.
      CHECK(q.size() == size - idx);
      for (int out_idx = q.size() - 1; out_idx >= 0; out_idx--) {
        const uint32_t value = q[out_idx];

        for (int bit = 0; bit < power; bit++) {
          // Flip the single bit.
          const int oidx = idx ^ (1 << bit);
          // XXX: Oops, the oidx might be larger and not yet
          // populated, duh! So we could greedily allocate it
          // such that it has the property?
          CHECK(oidx >= 0 && oidx < ret.size()) <<
            idx << " " << bit << " " << oidx;
          const uint32_t ovalue = ret[oidx];

          const uint32_t diff = value ^ ovalue;
          const int diffsize = std::popcount<uint32_t>(diff);
          if (diffsize != avalanche_target) goto next_oidx;
        }
        // All bits passed, so we found an index.
        found_idx = true;
        ret.push_back(value);
        // .. and consume the index.
        q.erase(q.begin() + out_idx);
        break;

      next_oidx:;
      }
      // No index was found that works, so try again.
      if (!found_idx) goto next_attempt;

      // ... otherwise keep looping over the indices.
    }

    // We found a working value for every index, so we are done.
    CHECK(ret.size() == size);
    // XXX Could check that it's actually a permutation, etc.
    return ret;

  next_attempt:;
  }
}

struct IndexQueue {
  IndexQueue(ArcFour *rc, size_t size) : rc(rc), size(size) {
    indices.reserve(size);
  }

  template<class F>
  std::optional<int> GetIndexMatching(const F &f) {
    for (int idx = indices.size() - 1; idx >= 0; idx--) {
      int v = indices[idx];
      if (f(v)) {
        indices.erase(indices.begin() + idx);
        return {v};
      }
    }
    return std::nullopt;
  }

  bool Empty() const { return indices.empty(); }

  template<class F>
  bool ContainsMatching(const F &f) {
    for (int v : indices) if (f(v)) return true;
    return false;
  }

  void Reset() {
    indices.clear();
    for (int i = 0; i < size; i++) indices.push_back(i);
    Shuffle(rc, &indices);
  }

  ArcFour *rc = nullptr;
  size_t size = 0;
  std::vector<int> indices;
};

// Second attempt. Assigns all the affected slots at once.
static std::vector<int> MakeAvalancheSubst(
    ArcFour *rc,
    size_t size) {

  static constexpr bool VERBOSE = false;

  CHECK(std::has_single_bit(size)) <<
    "size must be a power of two";
  const int power = std::countr_zero(size);
  CHECK((1 << power) == size);

  int64 tries = 0;
  std::vector<int64> failedat(size, 0LL);

  const int avalanche_target = power >> 1;

  IndexQueue q(rc, size);

  // If -1, then unassigned.
  std::vector<int> ret(size, -1);

  Timer run_timer;
  for (;;) {
    // -1 means unassigned.
    for (int i = 0; i < size; i++) ret[i] = -1;
    q.Reset();
    if (VERBOSE) printf(AGREEN(" == Restart ==") "\n");

    tries++;
    if ((tries % 10000000) == 0) {
      printf(ABLUE("%lld") " tries, " ACYAN("%.3f") "/sec.\n",
             tries, tries / run_timer.Seconds());
      for (int d = 0; d < size; d++) {
        printf("Failed at depth " APURPLE("%d") ": " ARED("%lld") " times\n",
               d, failedat[d]);
      }
    }

    for (int idx = 0; idx < size; idx++) {
      if (VERBOSE) {
        printf("Cur:");
        for (int i = 0; i < size; i++) {
          if (i == idx) {
            printf(ABLUE(" %d"), ret[i]);
          } else {
            printf(" %d", ret[i]);
          }
        }
        printf("\n");
      }

      // Already assigned.
      if (ret[idx] != -1) continue;

      std::optional<int> xo = q.GetIndexMatching(
          [power, avalanche_target, &ret, &q, idx](uint32_t value) {
            if (VERBOSE) printf("  Try " APURPLE("%d") "...\n", value);
            for (int bit = 0; bit < power; bit++) {
              // Flip the single bit.
              const int oidx = idx ^ (1 << bit);

              const uint32_t ovalue = ret[oidx];
              if (ovalue == -1) {
                // Need to have some index that matches the property.
                // Could also check that ovalue is not already the claimed
                // value, but the nonzero diff also implies this.
                if (!q.ContainsMatching([avalanche_target, value](const uint32_t ovalue) {
                    const uint32_t diff = value ^ ovalue;
                    const int diffsize = std::popcount<uint32_t>(diff);
                    return diffsize == avalanche_target;
                  })) return false;
              } else {
                // Or else the index that's there must work.
                const uint32_t diff = value ^ ovalue;
                const int diffsize = std::popcount<uint32_t>(diff);
                if (diffsize != avalanche_target) {
                  if (VERBOSE)
                    printf("  .. " ARED("rejected") " on bit %d (oidx %d) because "
                           "diffsize is %d\n", bit, oidx, diffsize);
                  return false;
                }
              }
            }
            // All bits ok.
            return true;
          });

      if (!xo.has_value()) {
        if (VERBOSE) printf(ARED(" .. no possible index.") "\n");
        failedat[idx]++;
        goto next_attempt;
      }

      // Assign it.
      const uint32_t value = xo.value();
      ret[idx] = value;

      // And all the related entries.
      for (int bit = 0; bit < power; bit++) {
        // Flip the single bit.
        const int oidx = idx ^ (1 << bit);

        const uint32_t ovalue = ret[oidx];
        if (ovalue == -1) {
          // Need to assign something.
          std::optional<int> oo =
            q.GetIndexMatching([avalanche_target, value](const uint32_t ovalue) {
                const uint32_t diff = value ^ ovalue;
                const int diffsize = std::popcount<uint32_t>(diff);
                return diffsize == avalanche_target;
              });

          // Even though we checked that *some* value existed, we
          // may have claimed it for an earlier slot.
          if (!oo.has_value()) {
            if (VERBOSE) printf(ARED(" .. no possible related index.") "\n");
            failedat[idx]++;
            goto next_attempt;
          }

          ret[oidx] = oo.value();
        }
      }

      // As long as we didn't fail above, on to the next idx.
    }

    // We found a working value for every index, so we are done.
    CHECK(ret.size() == size);
    CHECK(q.Empty());
    // XXX Could check that it's actually a permutation, etc.
    return ret;

  next_attempt:;
  }
}


// Make a permutation with a particular form. The mask argument
// is the same length as the returned permutation, and has a
// cycle id at each position. The returned vector is a randomly
// chosen permutation that has disjoint maximal-length cycles
// as specified.
static std::optional<std::vector<int>> MakePermutation(
    ArcFour *rc,
    const std::vector<int> &mask,
    bool require_avalanche) {

  std::vector<int> lens;
  lens.resize(mask.size());

  std::vector<int> ret;
  ret.resize(mask.size());

  for (int tries = 1; true; tries++) {
    // Maps cycle id to all positions in the vector with that id.
    std::map<int, std::vector<int>> cyclepos;
    for (int i = 0; i < mask.size(); i++)
      cyclepos[mask[i]].push_back(i);

    // Length of the desired cycle at each position.
    for (int i = 0; i < mask.size(); i++)
      lens[i] = (int)cyclepos[mask[i]].size();

    // Now shuffle those ids.
    for (auto &[k_, v] : cyclepos) Shuffle(rc, &v);
    // Generate the permutation.
    CHECK(mask.size() == ret.size());
    for (int i = 0; i < mask.size(); i++) {
      const int cycle_id = mask[i];
      auto &v = cyclepos[cycle_id];
      CHECK(!v.empty());
      ret[i] = v.back();
      v.pop_back();
    }

    // Now, check that the cycles are actually maximal.
    // PERF only really need to check this for one
    // element of each cycle.
    for (int i = 0; i < lens.size(); i++) {
      if (CycleLengthAt(ret, i) != lens[i]) goto again;
    }

    if (require_avalanche) {
      // Now we want the S-boxes to satisfy the avalanche criteria. We
      // can use the "strict avalache criteria", which is that changing
      // each bit of the input changes exactly half of the output bits.
      CHECK(std::has_single_bit(mask.size())) <<
        "mask must be a power of two";
      int power = std::countr_zero(mask.size());
      CHECK((1 << power) == mask.size());

      // Half of the bits.
      const int avalanche_target = power >> 1;

      for (int idx = 0; idx < mask.size(); idx++) {
        const uint32_t value = ret[idx];
        for (int bit = 0; bit < power; bit++) {
          // Flip the one bit.
          const int oidx = idx ^ (1 << bit);
          CHECK(oidx >= 0 && oidx < ret.size()) <<
            idx << " " << bit << " " << oidx;
          const uint32_t ovalue = ret[oidx];

          const uint32_t diff = value ^ ovalue;
          const int diffsize = std::popcount<uint32_t>(diff);
          // printf("%d want %d\n", diffsize, avalanche_target);
          if (diffsize != avalanche_target) goto again;
        }
      }
    }

    printf("OK\n");
    return ret;

  again:;
    if (tries == 100000) {
      return std::nullopt;
    }
  }
}

static bool IsMaximalCycle(const std::vector<int> &v) {
  return CycleLengthAt(v, 0) == v.size();
}

static void Test2Bit() {
  ArcFour rc("2bit");
  constexpr int BITS = 2;
  constexpr int SIZE = 1 << BITS;
  constexpr int NUM = 4;

  std::vector<std::vector<int>> perms;
  // Generate masks that have the properties we want.
  std::set<std::vector<int>> masks;
  while (perms.size() < NUM) {

    // Find a mask that is not yet
    std::vector<int> mask;
    do {
      mask.clear();
      for (int i = 0; i < 2; i++) mask.push_back(0);
      for (int i = 0; i < 2; i++) mask.push_back(1);
      // for (int i = 0; i < SIZE; i++) mask.push_back(0);
      CHECK(mask.size() == SIZE);
      Shuffle(&rc, &mask);
    } while (masks.find(mask) != masks.end());

    auto permo = MakePermutation(&rc, mask, true);
    if (permo.has_value()) {
      masks.insert(mask);
      std::vector<int> perm = std::move(permo.value());
      for (int x : perm) printf("%d, ", x);
      perms.push_back(std::move(permo.value()));
    }
  }

  CHECK(perms.size() == NUM);
  for (const std::vector<int> &perm : perms) {
    printf(" {");
    for (int x : perm) printf("%d, ", x);
    printf(" },\n");
  }

}

static void Do4Bit() {
  ArcFour rc("2bit");
  constexpr int BITS = 4;
  constexpr int SIZE = 1 << BITS;
  constexpr int NUM = 16;

  std::vector<std::vector<int>> perms;
  // Generate masks that have the properties we want.
  std::set<std::vector<int>> masks;
  while (perms.size() < NUM) {

    // Find a mask that is not yet
    std::vector<int> mask;
    do {
      mask.clear();
      for (int i = 0; i < SIZE / 2; i++) mask.push_back(0);
      for (int i = 0; i < SIZE / 2; i++) mask.push_back(1);
      // for (int i = 0; i < SIZE; i++) mask.push_back(0);
      CHECK(mask.size() == SIZE);
      Shuffle(&rc, &mask);
    } while (masks.find(mask) != masks.end());

    auto permo = MakePermutation(&rc, mask, true);
    if (permo.has_value()) {
      masks.insert(mask);
      std::vector<int> perm = std::move(permo.value());
      for (int x : perm) printf("%d, ", x);
      perms.push_back(std::move(permo.value()));
    }
  }

  CHECK(perms.size() == NUM);
  for (const std::vector<int> &perm : perms) {
    printf(" {");
    for (int x : perm) printf("%d, ", x);
    printf(" },\n");
  }

}

static void MakeExactAvalanche() {
  ArcFour rc("exact-avalanche");

  const int NUM = 16;
  std::set<std::vector<int>> perms;
  while (perms.size() < NUM) {
    std::vector<int> s = MakeAvalancheSubst(&rc, 16);
    if (perms.find(s) == perms.end()) {
      printf("Got one!\n");
      perms.insert(s);
    }
  }

  CHECK(perms.size() == NUM);
  for (const std::vector<int> &perm : perms) {
    printf(" {");
    for (int x : perm) printf("%d, ", x);
    printf(" },\n");
  }
}


int main(int argc, char **argv) {
  AnsiInit();

  MakeExactAvalanche();

  // RandStats();
  // AESStats();

  // Do4Bit();
  return 0;

  DB db;
  db.LoadFile("basis.txt");

  ImageRGBA img(1920, 1920);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  // We want the permutations to have cycles with different, and
  // relatively prime, lengths. We also want these to be in different
  // positions.
  // Primes less than 16: 2, 3, 5, 7, 11, 13,
  // So maybe 5+11 is the way to go?

  ArcFour rc("perms");

#if 0
  // Generate masks that have the properties we want.
  std::set<std::vector<int>> masks;
  for (int m = 0; m < 16; m++) {

    // Find a mask that is not yet
    std::vector<int> mask;
    do {
      mask.clear();
      for (int i = 0; i < 5; i++) mask.push_back(0);
      for (int i = 0; i < 11; i++) mask.push_back(1);
      CHECK(mask.size() == 16);
      Shuffle(&rc, &mask);
    } while (masks.find(mask) != masks.end());

    masks.insert(mask);
  }

  CHECK(masks.size() == 16);
  for (const std::vector<int> &mask : masks) {
    std::vector<int> perm = MakePermutation(&rc, mask, false);

    printf(" {");
    for (int x : perm) printf("%d, ", x);
    printf(" },\n");
  }
#endif

  std::vector<std::vector<int>> perms;
  // Generate masks that have the properties we want.
  std::set<std::vector<int>> masks;
  while (perms.size() < 16) {

    // Find a mask that is not yet
    std::vector<int> mask;
    do {
      mask.clear();
      // for (int i = 0; i < 3; i++) mask.push_back(0);
      // for (int i = 0; i < 13; i++) mask.push_back(1);
      for (int i = 0; i < 16; i++) mask.push_back(0);
      CHECK(mask.size() == 16);
      Shuffle(&rc, &mask);
    } while (masks.find(mask) != masks.end());

    auto permo = MakePermutation(&rc, mask, true);
    if (permo.has_value()) {
      masks.insert(mask);
      perms.push_back(std::move(permo.value()));
    }
  }

  CHECK(perms.size() == 16);
  for (const std::vector<int> &perm : perms) {
    printf(" {");
    for (int x : perm) printf("%d, ", x);
    printf(" },\n");
  }



#if 0
  for (int p = 0; p < 16; p++) {
    std::vector<int> perm;
    for (int i = 0; i < 16; i++) perm.push_back((i + 1) & 15);
    CHECK(IsMaximalCycle(perm)) << "simple cycle";
    do {
      Shuffle(&rc, &perm);
    } while (!IsMaximalCycle(perm));

    printf(" {", p);
    for (int x : perm) printf("%d, ", x);
    printf(" },\n");
  }
#endif
  return 0;


  int idx = 0;
  std::map<DB::key_type, const Exp *> sorted;
  for (const auto &[k, v] : db.fns) sorted[k] = v;
  for (const auto &[k, v] : sorted) {

    float h = idx / (float)Choppy::GRID;

    const auto [r, g, b] = ColorUtil::HSVToRGB(h, 1.0, 1.0);
    const uint32 color = ColorUtil::FloatsTo32(r, g, b, 0.75);

    printf("%s:\n%s\n\n",
           DB::KeyString(k).c_str(),
           Exp::ExpString(v).c_str());

    Table result = Exp::TabulateExpression(v);
    GradUtil::Graph(result, color, &img, idx * 2);

    img.BlendText32(2, idx * 10, color,
                    StringPrintf("%d. %s", idx, Exp::ExpString(v).c_str()));

    VerboseChoppy(v, &img);

    idx++;
  }

  img.Save("makesubst.png");
  printf("Wrote makesubst.txt\n");

  return 0;
}
