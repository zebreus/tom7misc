
#ifndef _CC_LIB_OPT_LARGE_OPTIMIZER_H
#define _CC_LIB_OPT_LARGE_OPTIMIZER_H

// Wrapper around black-box optimization, for optimization
// problems with a large number of parameters.
//
// This optimizes a subset of the parameters using the black-box
// optimizer, greedily takes any improvement, and repeats. Since it
// does not restart, it can easily get stuck in local minima. Since
// there are many possible subsets of parameters, it may not find
// an improvement even when a local one does exist. Therefore this
// is best suited to fairly easy convex problems.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

template<bool CACHE = true>
struct LargeOptimizer {

  // The first component is the score; lower is better.
  // The second parameter is true if this argument is feasible;
  // GetBest will only consider such arguments.
  using return_type = std::pair<double, bool>;

  using arg_type = std::vector<double>;

  // function to optimize.
  using function_type =
    std::function<return_type(const std::vector<double> &)>;

  // Convenience constants for inputs where the function cannot even be
  // evaluated. However, optimization will be more efficient if you
  // instead return a penalty that exceeds any feasible score, and has
  // a gradient towards the feasible region.
  static inline constexpr double LARGE_SCORE =
    std::numeric_limits<double>::max();
  static inline constexpr return_type INFEASIBLE =
    std::make_pair(LARGE_SCORE, false);

  LargeOptimizer(function_type f,
                 // Number of arguments.
                 int n,
                 uint64_t start_seed = 1);

  // Each argument needs to be described for the search procedure.
  // We have both the absolute lower and upper bounds for the argument
  // (the function being optimized is only called with values in that
  // range) and the downward and upward search limits (in each optimization
  // pass, we consider only values this far from the current best). The
  // default for the search limits optimize within the entire bounds.
  // Although we use extreme defaults here, it will not work well to
  // use extremely large double bounds, as internally the optimizer
  // wants to be able to work with "high - low" as a number that has
  // reasonable precision.
  //
  // The argument can be an integer with bounds [low, high),
  // or a double with bounds [low, high]. If integral, the function
  // is only called with an integer in that position (but represented
  // as a double). All 32-bit integers can be represented exactly
  // as doubles.
  using arginfo =
    std::variant<std::tuple<int32_t, int32_t, int32_t, int32_t>,
                 std::tuple<double, double, double, double>>;
  static arginfo Double(double low, double high,
                        double down = -1.0/0.0,
                        double up = 1.0/0.0) {
    return arginfo(std::make_tuple(low, high, down, up));
  }

  static arginfo Integer(
      int32_t low, int32_t high,
      int32_t down = std::numeric_limits<int32_t>::lowest(),
      int32_t up = std::numeric_limits<int32_t>::max()) {
    return arginfo(std::make_tuple(low, high, down, up));
  }

  // It is currently required to call AddResult or Sample with a known
  // reasonable solution before calling Run. We only optimize subsets
  // of the parameters at a time (and so we need something feasible to
  // use for the rest of them).

  // Consider a candidate from a previous run or known feasible
  // solution.
  // Assumes f(arg) = {{arg_score, true}}
  void AddResult(const arg_type &arg, double score);

  // Force sampling this arg, for example if we know an existing
  // feasible argument from a previous round but not its score. Must
  // be feasible.
  void Sample(const arg_type &arg);

  // Optimize until a termination condition is reached. Can be called
  // multiple times. Must call AddResult or Sample with a feasible
  // argument first.
  void Run(
      // Information about the arguments.
      const std::vector<arginfo> &arginfos,

      // Termination conditions. Stops if any is attained; at
      // least one should be set!
      // Maximum actual calls to f. Note that since calls are
      // cached, if the argument space is exhaustible, you
      // may want to set another termination condition as well.
      std::optional<int> max_calls,
      // Maximum feasible calls (f returns an output). Same
      // caution as above.
      std::optional<int> max_feasible_calls = std::nullopt,
      // Walltime seconds. Typically we run over the budget by
      // some unspecified amount.
      std::optional<double> max_seconds = std::nullopt,
      // Stop as soon as we have any output with a score <= target.
      std::optional<double> target_score = std::nullopt,
      // The maximum number of parameters to optimize at a time.
      int params_per_pass = 24,
      // If present, then only arguments marked true are optimized
      // in this call.
      const std::optional<std::vector<bool>> &arg_mask = {});

  // Get the best argument we found (with its score), if
  // any were feasible.
  std::optional<std::pair<arg_type, double>> GetBest() const;

  int64_t NumEvaluations() const { return evaluations; }

 private:
  const function_type f;

  // best value so far, if we have one
  std::optional<std::pair<arg_type, double>> best;

  struct HashArg {
    size_t operator ()(const arg_type &arg) const;
  };

  void MaybeSaveResult(const arg_type &arg,
                       std::pair<double, bool> res);

  // Number of arguments.
  int n = 0;

  // cache of previous results; only used if CACHE template parameter
  // is true. This is useful because the underlying optimizer works in
  // the space of doubles, and so we are likely to test the same
  // rounded integral arg multiple times. Cache is not cleaned.
  std::unordered_map<arg_type, std::pair<double, bool>, HashArg>
  cached_score;

  // Number of times we evaluated the target function.
  int64_t evaluations = 0;
  // seed1 and seed2 always nonzero.
  uint32_t seed1 = 1, seed2 = 2;
};

// There are only two possible instantiations, so they are implemented
// in the .cc file.
extern template struct LargeOptimizer<true>;
extern template struct LargeOptimizer<false>;

#endif
