
#include <vector>
#include <tuple>
#include <algorithm>

#include "arcfour.h"
#include "randutil.h"

#include "base/logging.h"
#include "base/stringprintf.h"

#include "plugins.h"

using namespace std;

struct Histogram {
  std::vector<double> samples;

  void Add(double f) {
    samples.push_back(f);
  }

  // Renders to num_buckets. Returns min bucket, bucket width, and a
  // normalized count for each bucket (max 1.0).
  std::tuple<double, double, vector<double>> Render(int num_buckets) {
    CHECK(!samples.empty());
    std::sort(samples.begin(), samples.end());
    double lb = samples.front();
    double ub = samples.back();
    double width = ub - lb;
    if (width == 0.0f) {
      // Degenerate
      std::vector<double> one = {1.0f};
      return std::make_tuple(lb, 0.0f, one);
    }

    double bucket_size = width / num_buckets;
    // First, count each bucket.
    vector<int> counts(num_buckets, 0);
    for (double f : samples) {
      int bucket = (f - lb) / bucket_size;
      // Expected for the single sample at the upper bound exactly.
      if (bucket == num_buckets) bucket--;
      CHECK(bucket >= 0 && bucket < num_buckets) << bucket;
      counts[bucket]++;
    }

    int max_count = 0;
    for (int c : counts) {
      max_count = std::max(c, max_count);
    }

    // There is at least one sample...
    CHECK(max_count > 0);
    vector<double> res;
    res.reserve(num_buckets);
    for (int c : counts)
      res.push_back(c / (double)max_count);

    return std::make_tuple(lb, bucket_size, std::move(res));
  }
};

int main(int argc, char **argv) {
  ArcFour rc(StringPrintf("plot.%lld", time(nullptr)));
  constexpr int NUM_SAMPLES = 1000000;

  using Plugin = Decimate<1024>;
  constexpr int P = 0;
  
  constexpr float BETA_A = Plugin::PARAMS[P].beta_a;
  constexpr float BETA_B = Plugin::PARAMS[P].beta_b;
  constexpr float MIN = Plugin::PARAMS[P].lb;
  constexpr float MAX = Plugin::PARAMS[P].ub;

  Histogram histo;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    double s = RandomBeta(&rc, BETA_A, BETA_B);
    s *= (MAX - MIN);
    s += MIN;
    // printf("%.04f\n", s);
    histo.Add(s);
  }

  const auto &[lb, w, buckets] = histo.Render(24);

  printf("\n");
  for (int i = 0; i < buckets.size(); i++) {
    float b = lb + w * i;
    printf("% 9.2f |", b);
    int s = buckets[i] * 60;
    for (int j = 0; j < s; j++)
      printf("*");
    printf("\n");
  }

  return 0;
}
