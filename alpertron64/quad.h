#ifndef _QUAD_H
#define _QUAD_H

#include <string>
#include <vector>
#include <utility>

#include "bignum/big.h"

struct LinearSolution {
  // Solutions of the form
  //   x = M_x t + B_x
  //   y = M_y t + B_y
  BigInt MX, BX;
  BigInt MY, BY;
};

struct PointSolution {
  BigInt X, Y;
};

struct Solutions {
  // Any (x,y).
  bool any_integers = false;
  bool interesting_coverage = false;
  std::vector<LinearSolution> linear;
  std::vector<PointSolution> points;
};

// If output is non-null, writes readable HTML there.
Solutions QuadBigInt(const BigInt &f);

#endif
