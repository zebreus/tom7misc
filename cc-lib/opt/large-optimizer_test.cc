#include "large-optimizer.h"

#include <utility>
#include <optional>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "timer.h"

using namespace std;

// Fairly easy optimization problem. Neighboring
// parameters should be close together; sum should
// be low. Best solution is all zeroes.

static std::pair<double, bool> F1(const std::vector<double> &args) {
#if 0
  printf("Call F1:");
  for (double d : args) printf(" %.3f", d);
  printf("\n");
#endif

  for (double d : args) {
    CHECK(std::isfinite(d));
    CHECK(d >= -100.0 && d <= 100.0);
  }

  double sum = 0.0;
  for (double d : args) sum += abs(d);

  double neighbors = 0.0;
  for (int i = 1; i < args.size(); i++) {
    double d = args[i] - args[i - 1];
    neighbors += sqrt(d * d);
  }

#if 0
  printf("Got %.5f + %.5f\n", sum, neighbors);
#endif
  return make_pair(sum + neighbors, true);
}

template<bool CACHE = true>
static void OptF1(int n) {
  Timer run_timer;
  using Optimizer = LargeOptimizer<CACHE>;
  Optimizer opt([n](const std::vector<double> v) {
      CHECK(v.size() == n);
      return F1(v);
    }, n, 0);

  // Needs a starting point that's feasible. Everything
  // is feasible for this problem.
  std::vector<double> example(n);
  for (int i = 0; i < n; i++) {
    example[i] = (((i ^ 97 + n) * 31337) % 200) - 100;
  }
  opt.Sample(example);

  std::vector<typename Optimizer::arginfo> arginfos(n);
  for (int i = 0; i < n; i++)
    arginfos[i] = Optimizer::Double(-100.0, 100.0);
  opt.Run(arginfos, {1000000}, nullopt, nullopt, {0.01});

  auto besto = opt.GetBest();
  CHECK(besto.has_value());
  const auto &v = besto.value().first;
  CHECK(v.size() == n) << v.size() << " vs " << n;
  printf("Best (score %.3f):", besto.value().second);
  for (int i = 0; i < v.size(); i++) {
    if (i < 20) {
      double d = v[i];
      printf(" %.3f", d);
    } else {
      printf(" ...");
      break;
    }
  }
  printf("\n");

  for (int i = 0; i < v.size(); i++) {
    double d = v[i];
    CHECK(d >= -0.01 && d <= 0.01) << "#" << i << ": " << d;
  }
  printf("OK with %d param(s) in %.3fs\n",
         n, run_timer.Seconds());
}

int main(int argc, char **argv) {

  // Make sure it doesn't fail if we have fewer parameters
  // than the internal sample size, for example.
  OptF1(1);

  // And make sure the function itself is sound...
  OptF1(5);

  // Small problem with more parameters than the round size.
  OptF1<true>(10);
  // ... and with cache disabled.
  OptF1<false>(10);

  OptF1<false>(1000);

  return 0;
}
