#include <stdio.h>
#include <string.h>
#include <cstdint>

#include <math.h>

#include "opt/optimizer.h"
#include "base/logging.h"

using namespace std;

// Two integral arguments, 0 doubles, "output type" is int.
using CircleOptimizer = Optimizer<2, 0, int>;

static pair<double, optional<int>>
DiscreteDistance(CircleOptimizer::arg_type arg) {
  auto [x, y] = arg.first;

  int sqdist = (x * x) + (y * y);
  if (sqdist >= 10 * 10)
    return CircleOptimizer::INFEASIBLE;
  return make_pair(-sqrt(sqdist), make_optional(sqdist));
}

static void CircleTest() {
  CircleOptimizer optimizer(DiscreteDistance);

  CHECK(!optimizer.GetBest().has_value());

  optimizer.SetBest({{0, 0}, {}}, 0.0, 0);

  CHECK(optimizer.GetBest().has_value());

  // Up to 900 calls.
  optimizer.Run({make_pair(-20, 20),
                 make_pair(-20, 20)}, {},
                {900}, nullopt, nullopt, nullopt);

  auto best = optimizer.GetBest();
  CHECK(best.has_value());

  auto [best_arg, best_score, best_sqdist] = best.value();
  auto [best_x, best_y] = best_arg.first;
  printf("Best (%d,%d) score %.3f sqdist %d\n",
         best_x, best_y,
         best_score, best_sqdist);

  // Should be able to find one of the corners.
  CHECK(best_sqdist == 98);
  CHECK(best_x == 7 || best_x == -7);
  CHECK(best_y == 7 || best_y == -7);
  CHECK(best_score < -9.898);
}

// Should be able to find an exact vector of integers.
static void ExactIntTest() {
  static constexpr std::array ints = {
    (int32_t)1, 2, 3, 4,
    -1, -2, -3, -4,
    -314159, -2653589, 0, -141421356,
    314159, 2653589, 0, 141421356,
    2, 4, 8, 16,
    32, 64, 128, 256,
    512, 1024, 2048, 4096,
    -3, -5, -3, -7,
    11011011, 22022022, -33033033, -44044044,
    8675309,8675309,18000000,18000000,
    -9999999, -888888, -7777, -666,
    -1582, 34971, -459273, -111511,
    0, 0, 0, 0,
    2, 8, 4, 27,
    -666666666, 3, 1, 3,
    0, 300000000, 0, 7,
  };

  using ExactIntOptimizer = Optimizer<ints.size(), 0, int>;
  ExactIntOptimizer::function_type OptimizeMe =
    [](const ExactIntOptimizer::arg_type &arg) ->
    ExactIntOptimizer::return_type {
      const auto &[intargs, doubles_] = arg;
      double sqdiff = 0.0;
      CHECK(intargs.size() == ints.size());
      for (int i = 0; i < intargs.size(); i++) {
        double d = intargs[i] - ints[i];
        sqdiff += (d * d);
      }
      return std::make_pair(sqdiff, std::make_optional(0));
    };

  ExactIntOptimizer optimizer(OptimizeMe);

  array<pair<int32_t, int32_t>, ints.size()> int_bounds;
  for (int i = 0; i < ints.size(); i++) {
    int_bounds[i] = make_pair(-0x7FFFFFFE, +0x7FFFFFFE);
  }

  optimizer.Run(int_bounds, {},
                // Up to 40960 calls
                {4096 * ints.size()}, nullopt, nullopt, nullopt);

  CHECK(optimizer.GetBest().has_value());
  const auto [bestarg, score, bestout_] =
    optimizer.GetBest().value();
  const auto &bestints = bestarg.first;
  CHECK(bestints.size() == ints.size());
  if (false) {
    printf("Score: %.4f\n", score);
    for (int i = 0; i < bestints.size(); i++) {
      printf("%d. %d\n", i, bestints[i]);
    }
  }
  for (int i = 0; i < bestints.size(); i++) {
    CHECK(bestints[i] == ints[i]) << i << ". Got "
                                  << bestints[i]
                                  << " want "
                                  << ints[i];
  }
}

int main(int argc, char **argv) {
  CircleTest();
  ExactIntTest();

  printf("OK\n");
  return 0;
}

