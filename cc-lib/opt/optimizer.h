
// Fancier wrapper around black-box optimizer (opt.h).
// Improvements here:
//   - function can take integral parameters in addition
//     to floating-point ones.
//   - function returns an output value in addition to
//     its score; we produce the overall best-scoring value
//   - termination condition specified either with number
//     of actual calls to underlying function to be optimized,
//     or wall time
//   - TODO: threads? could be a template arg and we just
//     avoid the overhead if THREADS=1
//   - TODO: serialize state?

#include <functional>
#include <array>
#include <optional>
#include <utility>
#include <limits>
#include <chrono>

#include "opt/opt.h"

template<int N_INTS, int N_DOUBLES, class OutputType>
struct Optimizer {

  static inline constexpr double LARGE_SCORE =
    std::numeric_limits<double>::max();
  
  // function takes two arrays as arguments: the int
  //   args and the doubles.
  using arg_type =
    std::pair<std::array<int32_t, N_INTS>,
              std::array<double, N_DOUBLES>>;
  
  // function to optimize.
  // return value is optional; if nullopt, this is an
  //   infeasible input (score = LARGE_SCORE).
  // if present, it is the score and the output value.
  using function_type =
    std::function<std::optional<std::pair<double, OutputType>>(arg_type)>;

  explicit Optimizer(function_type f);

  // Might already have a best candidate from a previous run or
  // known feasible solution. This is not currently used to inform
  // the search at all, but guarantees that GetBest will return a
  // solution at least as good as this one.
  // Assumes f(best_arg) = {{best_score, best_output}}
  void SetBest(arg_type best_arg, double best_score,
               OutputType best_output);

  // Run 
  void Run(
      // Finite {lower, upper} bounds on each argument. This can be
      // different for each call to run, in case you want to systematically
      // explore different parts of the space, or update bounds from a
      // previous solution.
      std::array<std::pair<int32_t, int32_t>, N_INTS> int_bounds,
      std::array<std::pair<double, double>, N_DOUBLES> double_bounds,
      // Termination conditions. Stops if any is attained; at
      // least one should be set!
      // Maximum actual calls to f. Note that since calls are
      // cached, if the argument space is exhaustible, you
      // may want to set another termination condition as well.
      std::optional<int> max_calls,
      // Maximum feasbile calls (f returns an output). Same
      // caution as above.
      std::optional<int> max_feasible_calls = std::nullopt,
      // Walltime seconds. Typically we run over the budget by
      // some unspecified amount.
      std::optional<double> max_seconds = std::nullopt,
      // Stop as soon as we have any output with a score <= target.
      std::optional<double> target_score = std::nullopt);

  // Get the best argument we found (with its score and output), if
  // any were feasible. 
  std::optional<std::tuple<arg_type, double, OutputType>> GetBest() const;

 private:
  static constexpr int N = N_INTS + N_DOUBLES;
  const function_type f;

  // best value so far, if we have one
  std::optional<std::tuple<arg_type, double, OutputType>> best;

  struct HashArg {
    size_t operator ()(const arg_type &arg) const {
      uint64_t result = 0xCAFEBABE;
      for (int i = 0; i < N_INTS; i++) {
        result ^= arg.first[i];
        result *= 0x314159265;
        result = (result << 13) | (result >> (64 - 13));
      }
      for (int i = 0; i < N_DOUBLES; i++) {
        result *= 0x1133995577;
        // This seems to be the most standards-compliant way to get
        // the bytes of a double?
        uint8_t bytes[sizeof (double)] = {};
        memcpy(&bytes, (const uint8_t *)&arg.second[i], sizeof (double));
        for (int j = 0; j < sizeof (double); j ++) {
          result ^= bytes[j];
          result = (result << 9) | (result >> (64 - 9));
        }
      }
      return (size_t) result;
    }
  };

  // cache of previous results. This is useful because the
  // underlying optimizer works in the space of doubles, and so
  // we are likely to test the same rounded integral arg multiple
  // times. Cache is not cleaned.
  std::unordered_map<arg_type, double, HashArg> cached_score;
};


// Template implementations follow.

template<int N_INTS, int N_DOUBLES, class OutputType>
Optimizer<N_INTS, N_DOUBLES, OutputType>::Optimizer(function_type f) :
  f(std::move(f)) {}

template<int N_INTS, int N_DOUBLES, class OutputType>
void Optimizer<N_INTS, N_DOUBLES, OutputType>::SetBest(
    arg_type best_arg, double best_score,
    OutputType best_output) {
  // Don't take it if we already have a better one.
  if (best.has_value() && std::get<1>(best.value()) < best_score)
    return;
  best.emplace(best_arg, best_score, std::move(best_output));
}

template<int N_INTS, int N_DOUBLES, class OutputType>
std::optional<std::tuple<
                typename Optimizer<N_INTS, N_DOUBLES, OutputType>::arg_type,
                double, OutputType>>
Optimizer<N_INTS, N_DOUBLES, OutputType>::GetBest() const {
  return best;
}

template<int N_INTS, int N_DOUBLES, class OutputType>
void Optimizer<N_INTS, N_DOUBLES, OutputType>::Run(
    std::array<std::pair<int32_t, int32_t>, N_INTS> int_bounds,
    std::array<std::pair<double, double>, N_DOUBLES> double_bounds,
    std::optional<int> max_calls,
    std::optional<int> max_feasible_calls,
    std::optional<double> max_seconds,
    std::optional<double> target_score) {
  const auto time_start = std::chrono::steady_clock::now();

  // Approach to rounding integers: ub is one past the largest integer
  // value to consider, lb is the lowest value considered. we pass the
  // same values as the double bounds. Then the sampled integer is
  // just floor(d), BUT: opt.h is not totally clear on whether the
  // upper bound is inclusive or exclusive. Since testing the actual
  // boundary seems like a reasonable thing, we handle the case
  // where the upper bound is sampled, by just decrementing it.
  std::array<double, N> lbs, ubs;
  for (int i = 0; i < N_INTS; i++) {
    lbs[i] = (double)int_bounds[i].first;
    ubs[i] = (double)int_bounds[i].second;
  }
  for (int i = 0; i < N_DOUBLES; i++) {
    lbs[N_INTS + i] = double_bounds[i].first;
    ubs[N_INTS + i] = double_bounds[i].second;
  }
  
  bool stop = false;
  // These are only updated if we use them for termination.
  int num_calls = 0, num_feasible_calls = 0;
  auto df = [this,
             max_calls, max_feasible_calls,
             max_seconds, target_score, time_start,
             &int_bounds,
             &stop, &num_calls, &num_feasible_calls](
      const std::array<double, N> &doubles) {
      if (stop) return LARGE_SCORE;

      // Test timeout first, to avoid cases where we have nearly
      // exhausted the input space.
      if (max_seconds.has_value()) {
        const auto time_now = std::chrono::steady_clock::now();
        const std::chrono::duration<double> time_elapsed =
          time_now - time_start;
        if (time_elapsed.count() > max_seconds.value())
          return LARGE_SCORE;
      }

      // Populate the native argument type.
      arg_type arg;
      for (int i = 0; i < N_INTS; i++) {
        int32_t a = (int32_t)doubles[i];
        // As described above; don't create invalid inputs if the
        // upper-bound is sampled exactly.
        if (a >= int_bounds[i].second) a = int_bounds[i].second - 1;
        arg.first[i] = a;
      }
      for (int i = 0; i < N_DOUBLES; i++) {
        const double d = doubles[i - N_INTS];
        arg.second[i] = d;
      }

      {
        // Have we already computed it?
        auto it = cached_score.find(arg);
        if (it != cached_score.end()) {
          return it->second;
        }
      }

      // Not cached, so this is a real call.
      if (max_calls.has_value()) {
        num_calls++;
        if (num_calls > max_calls.value()) {
          stop = true;
        }
      }
      
      auto res = f(arg);

      
      if (res.has_value()) {
        // Feasible call.
        if (max_feasible_calls.has_value()) {
          num_feasible_calls++;
          if (num_feasible_calls > max_feasible_calls.value()) {
            stop = true;
          }
        }

        const double score = res.value().first;
        
        cached_score[arg] = score;

        // First or new best? Save it.
        if (!best.has_value() || score < std::get<1>(best.value())) {
          best.emplace(arg, score, std::move(res.value().second));
        }

        if (score <= target_score)
          stop = true;
        
        return score;
      } else {
        cached_score[arg] = LARGE_SCORE;
            
        return LARGE_SCORE;
      }
    };

  // Run double-based optimizer on above.
  

  for (int seed = 1; !stop; seed++) {
    // PERF: Set biteopt parameters based on termination conditions,
    // or at least scale with number of parameters? Probably the
    // 'attempts' should actually be 1 here, since we have our own
    // attempts loop?
    Opt::Minimize<N>(df, lbs, ubs, 1000, 1, 10, seed);
  }
}
