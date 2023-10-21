
#include "quad.h"

#include <array>
#include <string>
#include <cstdio>
#include <cstdint>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "threadutil.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "timer.h"
#include "periodically.h"
#include "ansi.h"
#include "atomic-util.h"

static constexpr int MAX_COEFF = 12;
// Positive and negative, zero
static constexpr int RADIX = MAX_COEFF * 2 + 1;

using namespace std;

std::mutex file_mutex;

DECLARE_COUNTERS(count_any,
                 count_quad,
                 count_linear,
                 count_point,
                 count_recursive,
                 count_interesting,
                 count_none,
                 count_done);

static string CounterString() {
  return StringPrintf(ABLUE("%lld") " any "
                      AGREEN("%lld") " quad "
                      APURPLE("%lld") " lin "
                      ACYAN("%lld") " pt "
                      AYELLOW("%lld") " rec "
                      AGREY("%lld") " none "
                      ARED("%lld") " int",
                      count_any.Read(),
                      count_quad.Read(),
                      count_linear.Read(),
                      count_point.Read(),
                      count_recursive.Read(),
                      count_none.Read(),
                      count_interesting.Read());
}

// TODO: For cc-lib.
// Take one parameter, like "max amount of memory to use."
// Keep exact samples until we reach the memory budget;
// then use those samples to produce a bucketing. From then
// on, just accumulate into buckets.
struct AutoHisto {
  // Processed histogram for external rendering.
  struct Histo {
    std::vector<double> buckets;
    // The nominal left edge of the minimum bucket and right edge of the
    // maximum bucket, although these buckets actually contain data from
    // -infinity on the left and to +infinity on the right (i.e., these
    // are not the actual min and max samples). If the samples are
    // degenerate, we pick something so max > min.
    double min = 0.0, max = 0.0;
    // The width of each bucket (except the "open" buckets at the left
    // and right ends, which are infinite).
    double bucket_width = 0.0;
    // max - min. Always positive, even for degenerate data.
    double histo_width = 0.0;
    // The minimum and maximum value (count) of any bucket.
    // If the histogram is totally empty, this is set to [0, 1].
    double min_value = 0.0, max_value = 0.0;

    // Give the value of a bucket's left, right, or center.
    double BucketLeft(int idx) const { return min + bucket_width * idx; }
    double BucketRight(int idx) const { return BucketLeft(idx + 1); }
    double BucketCenter(int idx) const {
      return min + (bucket_width * (idx + 0.5));
    }
  };

  explicit AutoHisto(int64_t max_samples = 100000) :
    max_samples(max_samples) {
    CHECK(max_samples > 2);
  }

  void Observe(double x) {
    if (!std::isfinite(x))
      return;

    if (Bucketed()) {
      AddBucketed(x, &data);
    } else {
      data.push_back(x);
      if ((int64_t)data.size() >= max_samples) {
        // Transition to bucketed mode.

        // Sort data ascending so that it's easy to compute quantiles.
        std::sort(data.begin(), data.end());

        // XXX compute bounds, number of actual buckets
        min = data[1];
        double max = data[data.size() - 2];
        // XXX do something when samples are degenerate.
        CHECK(min < max);
        width = max - min;

        num_buckets = max_samples;

        std::vector<double> bucketed(num_buckets, 0.0);
        for (double d : data) AddBucketed(d, &bucketed);
        data = std::move(bucketed);
      }
    }
  }

  // Recommended to use a number of buckets that divides max_samples;
  // otherwise we get aliasing.
  Histo GetHisto(int buckets) const {
    CHECK(buckets >= 1);
    Histo histo;
    histo.buckets.resize(buckets, 0.0);

    if (Bucketed()) {

      histo.min = Min();
      histo.max = Max();
      histo.histo_width = width;
      const double bucket_width = width / buckets;
      histo.bucket_width = bucket_width;

      // Resampling the pre-bucketed histogram.
      for (int64_t b = 0; b < (int64_t)data.size(); b++) {
        // Original data. Use the center of the bucket as its value.
        double v = data[b];
        double center = min + ((b + 0.5) * BucketWidth());
        AddToHisto(&histo, center, v);
      }
      SetHistoScale(&histo);

    } else if (data.empty()) {
      // Without data, the histogram is degenerate.
      // Set bounds of [0, 1] and "max value" of 1.0.
      histo.min = 0.0;
      histo.max = 1.0;
      histo.histo_width = 1.0;
      histo.bucket_width = 1.0 / buckets;
      histo.min_value = 0.0;
      histo.max_value = 1.0;

    } else {

      // Compute temporary histogram from data. We have the
      // number of buckets.
      double minx = data[0], maxx = data[0];
      for (double x : data) {
        minx = std::min(x, minx);
        maxx = std::max(x, maxx);
      }

      if (maxx == minx) {
        // All samples are the same. We need the histogram to
        // have positive width, though.
        maxx = minx + 1;
      }

      CHECK(maxx > minx);
      histo.min = minx;
      histo.max = maxx;
      histo.histo_width = maxx - minx;
      histo.bucket_width = histo.histo_width / buckets;

      // Using the raw samples.
      for (double x : data) {
        AddToHisto(&histo, x, 1.0);
      }
      SetHistoScale(&histo);
    }

    return histo;
  }

  void PrintSimpleANSI(int buckets) const {
    const Histo histo = GetHisto(buckets);

    for (int bidx = 0; bidx < (int)histo.buckets.size(); bidx++) {
      const std::string label =
        PadLeft(StringPrintf("%.1f", histo.BucketLeft(bidx)), 10);
      static constexpr int BAR_CHARS = 60;
      double f = histo.buckets[bidx] / histo.max_value;
      int on = std::clamp((int)std::round(f * BAR_CHARS), 0, BAR_CHARS);
      std::string bar(on, '*');
      printf("%s | %s\n", label.c_str(), bar.c_str());
    }

  }

private:

  static std::string PadLeft(std::string s, int n) {
    while ((int)s.size() < n) s = " " + s;
    return s;
  }

  bool Bucketed() const { return num_buckets != 0; }
  // only when Bucketed
  double Min() const { return min; }
  double Max() const { return min + width; }
  double BucketWidth() const { return width / num_buckets; }

  static void SetHistoScale(Histo *h) {
    CHECK(!h->buckets.empty());
    double minv = h->buckets[0];
    double maxv = minv;
    for (double v : h->buckets) {
      minv = std::min(v, minv);
      maxv = std::max(v, maxv);
    }
    h->min_value = minv;
    h->max_value = maxv;
  }

  static void AddToHisto(Histo *h, double x, double count) {
    double f = (x - h->min) / h->histo_width;
    int64_t bucket = std::clamp((int64_t)(f * h->buckets.size()),
                                (int64_t)0,
                                (int64_t)h->buckets.size() - 1);
    h->buckets[bucket] += count;
  }

  void AddBucketed(double x, std::vector<double> *v) {
    double f = (x - min) / width;
    int64_t bucket = std::clamp((int64_t)(f * num_buckets),
                                (int64_t)0,
                                num_buckets - 1);
    (*v)[bucket]++;
  }

  // This either represents the exact data (until we exceed max_samples)
  // or the bucketed data (once we've decided on min, max, buckets).
  std::vector<double> data;
  double min = 0.0, width = 0.0;
  int64_t max_samples = 0;
  // If 0, then we're still collecting samples.
  int64_t num_buckets = 0;
};


static void RunGrid() {
  for (int i = 0; i < 80; i++)
    printf("\n");

  Periodically stats_per(5.0);
  Timer start_time;

  std::array<int64_t, 5> dims = {
    RADIX, RADIX, RADIX,
    RADIX, RADIX, /* f in loop */
  };

  // Microseconds.
  std::mutex histo_mutex;
  AutoHisto auto_histo(100000);

  ParallelCompND(
      dims,
      [&](const std::array<int64_t, 5> &arg,
          int64_t idx, int64_t total) {
        // Center on 0.
        // ... except a, since we already ran -10 to 0
        int64_t a = arg[0] + 21; // - MAX_COEFF;
        int64_t b = arg[1] - MAX_COEFF;
        int64_t c = arg[2] - MAX_COEFF;
        int64_t d = arg[3] - MAX_COEFF;
        int64_t e = arg[4] - MAX_COEFF;

        // Generalized version of known bad solution below.
        if (a == 0 && b == c)
          return;

        BigInt A(a);
        BigInt B(b);
        BigInt C(c);
        BigInt D(d);
        BigInt E(e);

        std::vector<double> local_timing;

        for (int64 f = -MAX_COEFF; f <= MAX_COEFF; f++) {

          // Known bad solutions.
          if (a == 0 && b == -6 && c == -6 &&
              d == -5 && e == 1 && f == 5)
            continue;

          BigInt F(f);

          auto Assert = [&](const char *type,
                            const BigInt &x, const BigInt &y) {
              BigInt r =
                A * (x * x) +
                B * (x * y) +
                C * (y * y) +
                D * x +
                E * y +
                F;
              CHECK(r == 0) << "\n\n\nInvalid solution (" << x.ToString()
                            << ", " << y.ToString() << ") of type " << type
                            << ".\nWant 0; result was: " << r.ToString()
                            << "\nProblem: "
                            << A.ToString() << " "
                            << B.ToString() << " "
                            << C.ToString() << " "
                            << D.ToString() << " "
                            << E.ToString() << " "
                            << F.ToString() << "\n\n\n\n";
            };

          Timer sol_timer;
          Solutions sols =
            QuadBigInt(A, B, C, D, E, F, nullptr);
          const double sol_usec = sol_timer.Seconds() * 1000000.0;
          local_timing.push_back(sol_usec);

          if (sols.interesting_coverage) {
            count_interesting++;
            printf("\n\n" APURPLE("Coverage!")
                   " %lld %lld %lld %lld %lld %lld\n\n",
                   a, b, c, d, e, f);
            MutexLock ml(&file_mutex);
            FILE *file = fopen("interesting-coverage.txt\n", "ab");
            CHECK(file != nullptr);
            fprintf(file, "%lld %lld %lld %lld %lld %lld\n",
                    a, b, c, d, e, f);
            fclose(file);
          }

          // Check solutions.
          if (sols.any_integers) {
            count_any++;
            Assert("any", BigInt(3), BigInt(7));
            Assert("any", BigInt(-31337), BigInt(27));
          }

          for (const LinearSolution &linear : sols.linear) {
            count_linear++;

            Assert("linear",
                   linear.MX * BigInt(0) + linear.BX,
                   linear.MY * BigInt(0) + linear.BY);

            Assert("linear",
                   linear.MX * BigInt(11) + linear.BX,
                   linear.MY * BigInt(11) + linear.BY);

            Assert("linear",
                   linear.MX * BigInt(-27) + linear.BX,
                   linear.MY * BigInt(-27) + linear.BY);
          }

          for (const PointSolution &point : sols.points) {
            count_point++;
            Assert("point", point.X, point.Y);
          }

          for (const QuadraticSolution &quad : sols.quadratic) {
            count_quad++;

            Assert("quad",
                   quad.VX * BigInt(0) + quad.MX * BigInt(0) + quad.BX,
                   quad.VY * BigInt(0) + quad.MY * BigInt(0) + quad.BY);

            Assert("quad",
                   quad.VX * BigInt(9) + quad.MX * BigInt(-3) + quad.BX,
                   quad.VY * BigInt(9) + quad.MY * BigInt(-3) + quad.BY);

            Assert("quad",
                   quad.VX * BigInt(100) + quad.MX * BigInt(10) + quad.BX,
                   quad.VY * BigInt(100) + quad.MY * BigInt(10) + quad.BY);
          };

          // TODO: Test recursive!
          for (const std::pair<RecursiveSolution,
                 RecursiveSolution> &rec : sols.recursive) {
            count_recursive++;
            (void)rec;
          }

          if (!sols.any_integers &&
              sols.linear.empty() &&
              sols.points.empty() &&
              sols.quadratic.empty() &&
              sols.recursive.empty()) {
            count_none++;
          }

          count_done++;
        }

        {
          MutexLock ml(&histo_mutex);
          for (double d : local_timing) {
            auto_histo.Observe(d);
          }
        }

        stats_per.RunIf([&]() {
            int64_t done = count_done.Read();
            double sec = start_time.Seconds();
            double qps = done / sec;
            double spq = sec / done;
            std::string timing = StringPrintf("%.3f solved/sec (%s ea.)",
                                              qps,
                                              ANSI::Time(spq).c_str());
            std::string counters = CounterString();
            std::string bar =
              ANSI::ProgressBar(
                  done, total,
                  StringPrintf("%lld %lld %lld %lld %lld * ",
                               a, b, c, d, e),
                  sec);

            static constexpr int STATUS_LINES = 3;

            static constexpr int HISTO_LINES = 20;

            for (int i = 0; i < (HISTO_LINES + STATUS_LINES); i++) {
              printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE);
            }

            // Histo
            {
              MutexLock ml(&histo_mutex);
              auto_histo.PrintSimpleANSI(HISTO_LINES);
            }

            printf("%s\n%s\n%s\n",
                   timing.c_str(),
                   counters.c_str(),
                   bar.c_str());
          });
        // printf("...\n");
      },
      12);

  printf("\n\n\n");

  std::string counters = CounterString();

  printf("Done in %s\n",
         ANSI::Time(start_time.Seconds()).c_str());
}



int main(int argc, char **argv) {
  ANSI::Init();

  RunGrid();

  return 0;
}
