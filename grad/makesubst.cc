
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

// Make a permutation with a particular form. The mask argument
// is the same length as the returned permutation, and has a
// cycle id at each position. The returned vector is a randomly
// chosen permutation that has disjoint maximal-length cycles
// as specified.
static std::vector<int> MakePermutation(
    ArcFour *rc,
    const std::vector<int> &mask) {

  std::vector<int> lens;
  lens.resize(mask.size());

  std::vector<int> ret;
  ret.resize(mask.size());

  for (;;) {
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

    return ret;

  again:;
  }
}

static bool IsMaximalCycle(const std::vector<int> &v) {
  return CycleLengthAt(v, 0) == v.size();
}

int main(int argc, char **argv) {
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
    std::vector<int> perm = MakePermutation(&rc, mask);

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
