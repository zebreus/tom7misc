
#ifndef _SOS_PREDICT_H
#define _SOS_PREDICT_H

#include <vector>
#include <cstdint>

#include "database.h"

struct Predict {

  // TODO: Newton's method code here.

  // Fit ys[x] = (c * x)^2.
  // Returns c and the error. x is 1-based; y[0] = 0 by definition.
  // In the context of the zeroes, x is the index here and y is actually
  // the x location of the zero.
  static std::pair<double, double> SquaredModel(
      const std::vector<int64_t> &ys);

  // Make a squared model, assuming that the x-coordinates are the
  // first n zeroes (like for herr). Return c and the error.
  static std::pair<double, double> DenseZeroesModel(
      const std::vector<int64_t> &zeroes);

  static std::pair<double, double> DensePrefixZeroesModel(
      const Database &db,
      const std::vector<int64_t> &zeroes);

  static int64_t NextInDenseSeries(const std::vector<int64_t> &zeroes);

  static int64_t NextInDensePrefixSeries(
      const Database &db,
      const std::vector<int64_t> &zeroes);

  // Use the zeroes to fit curves, then find all the points where they
  // will be close to coinciding. Return the predicted intersection
  // and its score (distance between the hzero and azero), sorted
  // by the score ascending.
  static std::vector<std::pair<int64_t, double>>
  FutureCloseCalls(
      const Database &db,
      const std::vector<int64_t> &azeroes,
      const std::vector<int64_t> &hzeroes,
      uint64_t max_inner_sum);


};

#endif
