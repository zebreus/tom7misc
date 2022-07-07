
#ifndef _GRAD_MAKEFN_OPS_H
#define _GRAD_MAKEFN_OPS_H

#include <array>
#include <utility>

#include "expression.h"

#include "half.h"

using half_float::half;

// Operation 1.
// Optimize this 4-parameter function:
//
// (x + c1) * 0.999 * ... * 0.999 * c3 - c4
//            `---- c2 times ---'
struct Op1 {
  static constexpr int INT_ARGS = 1;
  static constexpr int DOUBLE_ARGS = 3;

  static constexpr std::array<std::pair<int, int>, INT_ARGS>
  INT_BOUNDS = {
    make_pair(100, 600),
  };

  static constexpr std::array<std::pair<double, double>, DOUBLE_ARGS>
  DOUBLE_BOUNDS = {
    make_pair(-18.0, +18.0),
    make_pair(-18.0, +18.0),
    make_pair(-18.0, +18.0),
  };

  static const Exp *GetExp(Exp::Allocator *alloc,
                           const std::array<int, INT_ARGS> &ints,
                           const std::array<double, DOUBLE_ARGS> &dbls,
                           const Exp::Table &target) {
    const auto &[c2] = ints;
    const auto &[c1, c3, c4] = dbls;
    return
      alloc->PlusC(
          alloc->TimesC(
              alloc->TimesC(
                  // x + c1
                  alloc->PlusC(alloc->Var(), Exp::GetU16((half)c1)),
                  // * 0.999 ...
                  0x3bffu,
                  c2),
              Exp::GetU16((half)c3)),
          Exp::GetU16((half)c4));
  }
};

// Operation 2.
// (x * c1 + c2) - (c2 + c3)
struct Op2 {
  static constexpr int INT_ARGS = 0;
  static constexpr int DOUBLE_ARGS = 3;

  static constexpr std::array<std::pair<int, int>, INT_ARGS>
  INT_BOUNDS = {
  };

  static constexpr std::array<std::pair<double, double>, DOUBLE_ARGS>
  DOUBLE_BOUNDS = {
    make_pair(0.001, 1.25),
    make_pair(-128.0, +128.0),
    make_pair(-1.0, +1.0),
  };

  static const Exp *GetExp(Exp::Allocator *alloc,
                           const std::array<int, INT_ARGS> &ints,
                           const std::array<double, DOUBLE_ARGS> &dbls,
                           const Exp::Table &target) {
    const auto &[c1, c2, c3] = dbls;
    return
      alloc->PlusC(
          alloc->PlusC(
              alloc->TimesC(alloc->Var(), Exp::GetU16((half)c1)),
              Exp::GetU16((half)c2)),
          Exp::GetU16((half)(c2 + c3)));
  }
};

struct Op3 {
  static constexpr int INT_ARGS = 1;
  static constexpr int DOUBLE_ARGS = 4;

  static constexpr std::array<std::pair<int, int>, INT_ARGS>
  INT_BOUNDS = {
    make_pair(10, 700),
  };

  static constexpr std::array<std::pair<double, double>, DOUBLE_ARGS>
  DOUBLE_BOUNDS = {
    make_pair(-4, 4),
    make_pair(-4, 4),
    make_pair(-1.0, -0.1),
    make_pair(-10.0, 10.0),
  };

  static const Exp *GetExp(Exp::Allocator *alloc,
                           const std::array<int, INT_ARGS> &ints,
                           const std::array<double, DOUBLE_ARGS> &dbls,
                           const Exp::Table &target) {
    const auto &[iters] = ints;
    const auto &[c1, c2, c3, c4] = dbls;

    // Maybe should avoid reallocating this over and over
    const Exp *f1 =
      alloc->TimesC(
          alloc->PlusC(
              alloc->TimesC(
                  alloc->Var(),
                  // * 0.999 ...
                  0x3bffu,
                  500),
              0x8000),
          0x3d4b);

    return
      alloc->TimesC(
        alloc->PlusE(
            f1,
            alloc->TimesC(
                alloc->PlusC(
                    alloc->TimesC(
                        alloc->PlusC(alloc->Var(),
                                    // e.g. 0.67
                                    Exp::GetU16((half)c1)),
                        0x3c01u,
                        iters),
                    // e.g. -0.786.
                    // XX maybe use -c1 here to reduce the parameter
                    // space?
                    Exp::GetU16((half)c2)),
                // e.g. -0.783. Should be negative because we want to
                // subtract it from f1, but we could tune this
                Exp::GetU16((half)c3))),
        Exp::GetU16((half)c4));
  }
};

struct Op4 {
  static constexpr int INT_ARGS = 3;
  static constexpr int DOUBLE_ARGS = 2;

  static constexpr std::array<std::pair<int, int>, INT_ARGS>
  INT_BOUNDS = {
    make_pair(1, 20),
    make_pair(0, 4),
    make_pair(0, 4),
  };

  static constexpr std::array<std::pair<double, double>, DOUBLE_ARGS>
  DOUBLE_BOUNDS = {
    make_pair(-1.0, +1.0),
    make_pair(-4.0, +4.0)
  };

  static const Exp *GetExp(Exp::Allocator *alloc,
                           const std::array<int, INT_ARGS> &ints,
                           const std::array<double, DOUBLE_ARGS> &dbls,
                           const Exp::Table &target) {
    const auto &[iter_diff, u1, u2] = ints;
    const auto &[xd, scale] = dbls;

    const Exp *f0 =
      alloc->TimesC(
        alloc->PlusE(
            alloc->TimesC(
                alloc->PlusC(
                    alloc->TimesC(
                        // x - 4
                        alloc->PlusC(alloc->Var(), 0xc400),
                        // * 0.999 ...
                        0x3bffu - u1,
                        300 - iter_diff * 10),
                    0x42d4),
                0x3c00),
            alloc->TimesC(
                alloc->TimesC(
                    alloc->PlusC(
                        alloc->TimesC(
                            // x - 4
                            alloc->PlusC(alloc->Var(), 0xc400),
                            // * 0.999 ...
                            0x3bffu - u2,
                            300),
                        0x42d4),
                    0x3c00),
                Exp::GetU16(-1.0_h))),
        Exp::GetU16((half)scale));

    // Pick a point at which to analytically set the y values
    // equal (within the limits of precision).
    uint16_t x = Exp::GetU16((half)xd);
    half fy = Exp::GetHalf(Exp::EvaluateOn(f0, x));
    half ty = Exp::GetHalf(target[x]);
    uint16 yneg = Exp::GetU16(ty - fy);

    return alloc->PlusC(f0, yneg);
  }

};

#endif
