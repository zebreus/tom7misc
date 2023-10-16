#ifndef _QUAD_H
#define _QUAD_H

#include <string>
#include <vector>
#include <utility>

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

struct RecursiveSolution {
  // Solution via a recurrence relation. From any
  // starting solution x_0, y_0:
  //   x_(n+1) = P x_n + Q y_n + K
  //   y_(n+1) = R x_n + S y_n + L
  // K and L are often zero.
  BigInt P, Q, K;
  BigInt R, S, L;
};

struct Solutions {
  // Any (x,y).
  bool any_integers = false;
  bool interesting_coverage = false;
  std::vector<QuadraticSolution> quadratic;
  std::vector<LinearSolution> linear;
  std::vector<PointSolution> points;
  // Recursive solutions always come in related pairs.
  std::vector<std::pair<RecursiveSolution, RecursiveSolution>> recursive;
};

// If output is non-null, writes readable HTML there.
Solutions QuadBigInt(const BigInt &a, const BigInt &b, const BigInt &c,
                     const BigInt &d, const BigInt &e, const BigInt &f,
                     std::string *output);

#endif
