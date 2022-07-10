
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
#include "image.h"
#include "color-util.h"

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

      // Check from 0.0125 -- 0.9975 of the interval.
      // (Note that's still 2.5% of the whole
      // function that could be wrong!)
      // half low  = x + (half)(1 / (float)(GRID/2)) * (half)0.0125;
      // half high = x + (half)(1 / (float)(GRID/2)) * (half)0.9975;

      // Exact version!
      half low = x;
      half high = (half)(((i + 1) / (double)(GRID/2)) - 1.0);

      /*
      printf("%d. x=%.3f check %.3f to %.3f\n",
             i, (float)x, (float)low, (float)high);
      */

      for (half pos = low; pos < high; pos = nextafter(pos, high)) {
        uint16 v = Exp::EvaluateOn(exp, Exp::GetU16(pos));
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

    return {ret};
  }

  struct DB {
    Allocator alloc;
    std::mutex m;

    enum class AddResult {
      // Not added.
      NOT_CHOPPY,
      OUTSIDE_GRID,
      NOT_NEW,
      // Added.
      SUCCESS_NEW,
      SUCCESS_SMALLER,
    };

    void LoadFile(const std::string &filename) {
      std::vector<string> lines =
        Util::ReadFileToLines(filename);
      for (const string &line : lines) {
        if (line.empty()) continue;
        if (line[0] == '/') continue;
        string error;
        const Exp *e = Exp::Deserialize(&alloc, line, &error);
        CHECK(e != nullptr) << error << "\n" << line;
        // Can be rejected by new stricter criteria (e.g. outside_grid)
        // but nothing in the database should be invalid.
        CHECK(Add(e) != AddResult::NOT_CHOPPY) << "from line: " << line
                                               << "\n" << Exp::ExpString(e);
      }
    }

    // TODO: Any choppy function can be easily offset in the y
    // dimension (by just PlusC of 1/GRID) after the fact. So
    // we should normalize these, perhaps by centering on 0.
    // Integer scalings are also redundant (TimesC of an integer),
    // so perhaps we should also "reduce" them. But we can clean
    // this up later (see chopreduce.exe).

    // The expression is copied if it is inserted, so it can be
    // from another allocator, and can be invalidated after the
    // call returns.
    AddResult Add(const Exp *e) {
      CHECK(e != nullptr);
      auto go = GetChoppy(e);
      if (!go.has_value()) return AddResult::NOT_CHOPPY;
      const std::array<int, GRID> &id = go.value();

      // Exclude vectors whose coefficients are outside the grid.
      // These could potentially be useful but they
      // clog up the works.
      for (int i : id) {
        if (i < -(GRID/2) || i > (GRID/2)) {
          return AddResult::OUTSIDE_GRID;
        }
      }

      {
        std::unique_lock<std::mutex> ml(m);
        auto it = fns.find(id);
        if (it == fns.end()) {
          fns[id] = alloc.Copy(e);
          return AddResult::SUCCESS_NEW;
        } else {
          const int newsize = Exp::ExpSize(e);
          const int oldsize = Exp::ExpSize(it->second);
          if (newsize < oldsize) {
            fns[id] = alloc.Copy(e);
            return AddResult::SUCCESS_SMALLER;
          }
        }
      }
      return AddResult::NOT_NEW;
    }

    void DumpCode() {
      std::unique_lock<std::mutex> ml(m);
      std::map<key_type, const Exp *> sorted;
      for (const auto &[k, v] : fns) sorted[k] = v;
      printf("static constexpr const char *FNS[] = {\n");
      for (const auto &[k, v] : sorted) {
        printf("  //");
        for (int i : k) printf(" %d", i);
        printf("\n"
               "  \"%s\",\n",
               Exp::Serialize(v).c_str());
      }
      printf("};\n");
    }

    std::string Dump() {
      std::unique_lock<std::mutex> ml(m);
      std::string out;
      std::map<key_type, const Exp *> sorted;
      for (const auto &[k, v] : fns) sorted[k] = v;
      for (const auto &[k, v] : sorted) {
        // Optional comments
        StringAppendF(&out, "//");
        for (int i : k) StringAppendF(&out, " %d", i);
        string ser = Util::losewhitel(Exp::Serialize(v));
        StringAppendF(&out, "\n"
                      "%s\n",
                      ser.c_str());
      }
      return out;
    }

    ImageRGBA Image() {
      ImageRGBA img(GRID, fns.size());
      std::map<key_type, const Exp *> sorted;
      for (const auto &[k, v] : fns) sorted[k] = v;
      int y = 0;
      for (const auto &[k, v] : sorted) {
        for (int x = 0; x < GRID; x++) {
          int c = k[x];
          float r = 0.0f, g = 0.0f;
          if (c > 0) {
            g = 0.1 + 0.9 * (c / (float)(GRID/2.0f));
          } else if (c < 0) {
            r = 0.1 + 0.9 * ((-c) / (float)(GRID/2.0f));
          }

          float b = (x & 1) ? 0.1f : 0.0f;

          img.SetPixel32(x, y,
                         ColorUtil::FloatsTo32(r, g, b, 1.0));
        }
        y++;
      }
      return img;
    }

    using key_type = std::array<int, GRID>;
    static std::string KeyString(const key_type &k) {
      string out;
      for (int i = 0; i < GRID; i++) {
        if (i != 0) StringAppendF(&out, " ");
        StringAppendF(&out, " %d", k[i]);
      }
      return out;
    }

    // Protected by mutex.
    std::unordered_map<
      key_type, const Exp *, Hashing<key_type>> fns;
  };

};

#endif
