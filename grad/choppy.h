
#ifndef _GRAD_CHOPPY_H
#define _GRAD_CHOPPY_H

#include <optional>
#include <array>
#include <map>
#include <unordered_map>
#include <cstdio>
#include <cstdint>

#include "expression.h"
#include "half.h"
#include "hashing.h"

#include "choppy.h"

// A choppy function is a stepwise function that produces
// integer outputs on some grid. Here we use a grid of size 16,
// spanning from [-1,1] in half space on both axes, so the
// "integers" are 0, 1/8, -1/8, 2/8, -2/8, ... 8/8, -8,8.
// Internally we store these multiplied by (GRID/2), so they
// take values 0, 1, -1, 2, -2... GRID/2, -GRID/2.
//
// As a special consideration, we don't actually care about
// the behavior on inputs of exactly integers (e.g. x=0),
// just the segments in-between.

struct Choppy {
  static constexpr int GRID = 16;
  static constexpr double EPSILON = 0.0001;

  using Allocator = Exp::Allocator;

  static std::optional<std::array<int, GRID>> GetChoppy(
      const Exp *exp) {

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
        return {};
      }

      ret[i] = yy - (GRID / 2);
      val[i] = Exp::GetU16(y);
    }

    // Also check that the surrounding values are exactly equal.
    for (int i = 0; i < GRID; i++) {
      half x = (half)((i / (double)(GRID/2)) - 1.0);

      // Check from 0.10 -- 0.90 of the interval.
      half low  = x + (half)(i / GRID) * (half)0.10;
      half high = x + (half)(i / GRID) * (half)0.90;

      for (half pos = low; pos < high; pos = nextafter(pos, high)) {
        uint16 v = Exp::EvaluateOn(exp, Exp::GetU16(pos));

        if (val[i] != v) {
          // Not the same value for the interval.
          // (Maybe we could accept it if "really close"?)
          return {};
        }
      }
    }

    return {ret};
  }

  struct DB {
    Allocator alloc;

    // TODO: Any choppy function can be easily offset in the y
    // dimension (by just PlusC of 1/GRID) after the fact. So
    // we should normalize these, perhaps by centering on 0.
    // Integer scalings are also redundant (TimesC of an integer),
    // so perhaps we should also "reduce" them. But we can clean
    // this up later.
    bool Add(const Exp *e) {
      auto go = GetChoppy(e);
      if (!go.has_value()) return false;
      const std::array<int, GRID> &id = go.value();

      // Should probably replace it if the expression is smaller.
      auto it = fns.find(id);
      if (it == fns.end()) {
        fns[id] = e;
        return true;
      }
      return false;
    }

    void Dump() {
      std::map<key_type, const Exp *> sorted;
      for (const auto &[k, v] : fns) sorted[k] = v;
      for (const auto &[k, v] : sorted) {
        printf("  //");
        for (int i : k) printf(" %d", i);
        printf("\n"
               "  db->Add(%s);\n\n",
               Exp::ExpString(v).c_str());
      }
    }

    using key_type = std::array<int, GRID>;
    std::unordered_map<
      key_type, const Exp *, Hashing<key_type>> fns;

  };

};

#endif
