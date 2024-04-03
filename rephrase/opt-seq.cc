
#include "opt-seq.h"

#include <vector>
#include <optional>
#include <utility>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "opt/opt.h"
#include "base/logging.h"

OptSeq::OptSeq(
    const std::vector<std::pair<double, double>> &bounds) :
  bounds(bounds) {

  std::vector<double> lb, ub;
  for (const auto &[l, u] : bounds) {
    lb.push_back(l);
    ub.push_back(u);
  }

  std::function<double(const std::vector<double> &)> f =
    [this](const std::vector<double> &args) {
      printf("Called:");
      for (double d : args) printf(" %.4f", d);
      printf("\n");
      {
        std::unique_lock<std::mutex> ml(m);
        c.wait(ml, [this](){
            return !arg.has_value();
          });

        CHECK(!arg.has_value());
        arg = {args};
      }
      c.notify_one();

      {
        std::unique_lock<std::mutex> ml(m);
        c.wait(ml, [this](){
            return result.has_value();
          });

        double r = result.value();
        // Consume the result.
        result.reset();
        arg.reset();
        return r;
      }
    };

  printf("spawn...\n");
  th.reset(
      new std::thread([this, lb, ub, f]() {
          printf("START\n");

          Opt::Minimize(
              (int)lb.size(),
              f,
              lb, ub,
              1000,
              1,
              10,
              // Always use the same random seed so that we can
              // replay previous samples.
              0xCAFE);

        }));
}

std::vector<double> OptSeq::Next() {
  std::vector<double> ret;
  {
    std::unique_lock<std::mutex> ml(m);
    c.wait(ml, [this](){
        return arg.has_value();
      });

    ret = arg.value();
    // We leave the argument, since we need this to record
    // history/best.
  }
  c.notify_one();
  return ret;
}

void OptSeq::Result(double d) {
  {
    std::unique_lock<std::mutex> ml(m);
    CHECK(arg.has_value());
    CHECK(!result.has_value());

    Observe(arg.value(), d);

    result.emplace(d);
  }
  c.notify_one();
}

// With lock.
void OptSeq::Observe(const std::vector<double> &arg,
                     double d) {
  if (!best.has_value() ||
      best.value().second > d) {
    best.emplace(arg, d);
  }
  history.emplace_back(arg, d);
}

std::optional<std::pair<std::vector<double>, double>> OptSeq::GetBest() {
  return best;
}
