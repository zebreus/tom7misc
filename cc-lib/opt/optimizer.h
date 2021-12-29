
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
#include <unordered_map>

#include "opt/opt.h"

template<int N_INTS, int N_DOUBLES, class OutputType>
struct Optimizer {

  static inline constexpr int num_ints = N_INTS;
  static inline constexpr int num_doubles = N_DOUBLES;

  static inline constexpr double LARGE_SCORE =
    std::numeric_limits<double>::max();

  // Convenience constant for inputs where the function cannot even be
  // evaluated. However, optimization will be more efficient if you
  // instead return a penalty that exceeds any feasible score, and has
  // a gradient towards the feasible region.
  static inline constexpr
  std::pair<double, std::optional<OutputType>> INFEASIBLE =
    std::make_pair(LARGE_SCORE, std::nullopt);

  // function takes two arrays as arguments: the int
  //   args and the doubles.
  using arg_type =
    std::pair<std::array<int32_t, N_INTS>,
              std::array<double, N_DOUBLES>>;

  // Return value from the function being optimized, consisting
  // of a score and an output. If the output is nullopt, then
  // this is an infeasible input, and score should ideally give
  // some gradient towards the feasible region (and be bigger
  // than any score in the feasible region).
  using return_type = std::pair<double, std::optional<OutputType>>;

  // function to optimize.
  using function_type = std::function<return_type(arg_type)>;

  explicit Optimizer(function_type f);

  // Might already have a best candidate from a previous run or
  // known feasible solution. This is not currently used to inform
  // the search at all, but guarantees that GetBest will return a
  // solution at least as good as this one.
  // Assumes f(best_arg) = {{best_score, best_output}}
  void SetBest(arg_type best_arg, double best_score,
               OutputType best_output);

  // Force sampling this arg, for example if we know an existing
  // feasible argument from a previous round but not its result.
  void Sample(arg_type arg);

  // Run
  void Run(
      // Finite {lower, upper} bounds on each argument. This can be
      // different for each call to run, in case you want to systematically
      // explore different parts of the space, or update bounds from a
      // previous solution.
      // integer upper bounds exclude high: [low, high).
      // double upper bounds are inclusive: [low, high].
      std::array<std::pair<int32_t, int32_t>, N_INTS> int_bounds,
      std::array<std::pair<double, double>, N_DOUBLES> double_bounds,
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
      std::optional<double> target_score = std::nullopt);

  // Get the best argument we found (with its score and output), if
  // any were feasible.
  std::optional<std::tuple<arg_type, double, OutputType>> GetBest() const;

  // If set to true, the result of every function evaluation is saved and
  // can be queried later.
  void SetSaveAll(bool save);

  // Get all the evaluated results. The output types are only populated
  // if SaveAll was true when they were added (and the result was feasible).
  std::vector<std::tuple<arg_type, double, std::optional<OutputType>>>
  GetAll() const;

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
        for (size_t j = 0; j < sizeof (double); j ++) {
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

  // Same for the output. Not stored unless save_all is set.
  std::unordered_map<arg_type, std::optional<OutputType>,
                     HashArg> cached_output;
  bool save_all = false;
};


// Template implementations follow.

template<int N_INTS, int N_DOUBLES, class OutputType>
Optimizer<N_INTS, N_DOUBLES, OutputType>::Optimizer(function_type f) :
  f(std::move(f)) {}

template<int N_INTS, int N_DOUBLES, class OutputType>
void Optimizer<N_INTS, N_DOUBLES, OutputType>::SetSaveAll(bool save) {
  save_all = save;
}

template<int N_INTS, int N_DOUBLES, class OutputType>
void Optimizer<N_INTS, N_DOUBLES, OutputType>::Sample(arg_type arg) {
  auto [score, res] = f(arg);
  cached_score[arg] = score;
  if (save_all) cached_output[arg] = res;

  if (res.has_value()) {
    // First or improved best?
    if (!best.has_value() || score < std::get<1>(best.value())) {
      best.emplace(arg, score, std::move(res.value()));
    }
  }
}

template<int N_INTS, int N_DOUBLES, class OutputType>
void Optimizer<N_INTS, N_DOUBLES, OutputType>::SetBest(
    arg_type best_arg, double best_score,
    OutputType best_output) {
  // Add to cache no matter what.
  cached_score[best_arg] = best_score;
  if (save_all) cached_output[best_arg] = best_output;

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
auto Optimizer<N_INTS, N_DOUBLES, OutputType>::GetAll() const ->
std::vector<std::tuple<arg_type, double, std::optional<OutputType>>> {
  std::vector<std::tuple<arg_type, double, std::optional<OutputType>>> ret;
  ret.reserve(cached_score.size());
  for (const auto &[arg, score] : cached_score) {
    auto it = cached_output.find(arg);
    if (it != cached_output.end()) {
      ret.emplace_back(arg, score, it->second);
    } else {
      ret.emplace_back(arg, score, std::nullopt);
    }
  }

  return ret;
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
        if (time_elapsed.count() > max_seconds.value()) {
          stop = true;
          return LARGE_SCORE;
        }
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
        const double d = doubles[i + N_INTS];
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

      auto [score, res] = f(arg);
      cached_score[arg] = score;
      if (save_all) cached_output[arg] = res;

      if (res.has_value()) {
        // Feasible call.
        if (max_feasible_calls.has_value()) {
          num_feasible_calls++;
          if (num_feasible_calls > max_feasible_calls.value()) {
            stop = true;
          }
        }

        // First or new best? Save it.
        if (!best.has_value() || score < std::get<1>(best.value())) {
          best.emplace(arg, score, std::move(res.value()));
        }

        if (score <= target_score)
          stop = true;
      }
      return score;
    };

  // Run double-based optimizer on above.

  // PERF: Better set biteopt parameters based on termination conditions.
  // Linear scaling is probably not right.
  // Perhaps this could itself be optimized? 
  const int ITERS = 1000 * powf(N, 1.5f);
  
  for (int seed = 1; !stop; seed++) {
	// stop is set by the callback below, but g++ sometimes gets mad
	(void)(stop = !!stop);
    // PERF: Biteopt now has stopping conditions, so we should be able
	// to be more accurate here.
    Opt::Minimize<N>(df, lbs, ubs, ITERS, 1, 1, seed);
  }
}
