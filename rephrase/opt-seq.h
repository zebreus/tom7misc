
#ifndef _REPHRASE_OPT_SEQ_H
#define _REPHRASE_OPT_SEQ_H

#include <vector>
#include <optional>
#include <utility>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>

struct OptSeq {
  // Pair of [low, high] bounds for each parameter.
  OptSeq(const std::vector<std::pair<double, double>> &bounds);
  ~OptSeq();
  // TODO: Save/Load

  std::vector<double> Next();
  void Result(double d);

  std::optional<std::pair<std::vector<double>, double>> GetBest();

 private:
  void Observe(const std::vector<double> &arg, double d);
  void OptThread();
  double Eval(const std::vector<double> &args);

  std::mutex m;
  std::condition_variable c;
  std::unique_ptr<std::thread> th;

  std::vector<std::pair<double, double>> bounds;
  // An arg vector, pending for the next call to Next().
  std::optional<std::vector<double>> arg;
  // A result, pending to be consumed by the optimization thread.
  std::optional<double> result;
  // Previous sequence of optimized values.
  std::vector<std::pair<std::vector<double>, double>> history;

  std::optional<std::pair<std::vector<double>, double>> best;
  bool should_die = false;
};

#endif
