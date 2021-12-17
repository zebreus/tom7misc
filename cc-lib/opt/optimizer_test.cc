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

  // Up to 90 calls.
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

int main(int argc, char **argv) {
  CircleTest();

  printf("OK\n");
  return 0;
}

