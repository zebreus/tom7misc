#ifndef _QUAD_H
#define _QUAD_H

#include <string>

#include "bignum/big.h"

struct QuadraticSolution {
  // Solutions of the form
  //   x = V_x k^2 + M_x k + B_x
  //   y = V_y k^2 + V_y k + B_y
  BigInt VX, MX, BX;
  BigInt VY, MY, BY;
};

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
  std::vector<QuadraticSolution> quadratic;
  std::vector<LinearSolution> linear;
  std::vector<PointSolution> points;
};

// If output is non-null, writes readable HTML there.
void QuadBigInt(const BigInt &a, const BigInt &b, const BigInt &c,
                const BigInt &d, const BigInt &e, const BigInt &f,
                std::string *output);

#endif
