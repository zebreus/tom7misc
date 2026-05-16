
#include "albrecht.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "bit-string.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "periodically.h"
#include "randutil.h"
#include "sampler.h"
#include "solve-leaf.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"

using Aug = Albrecht::AugmentedPoly;
using OneSample = Sampler::OneSample;


DECLARE_COUNTERS(ctr_poly, ctr_samples, ctr_satisfied_leaf, ctr_saved);

static std::string FormatNum(uint64_t n) {
  if (n > 1'000'000) {
    double m = n / 1'000'000.0;
    if (m >= 1'000'000.0) {
      return std::format("{:.1f}T", m / 1'000'000.0);
    } else if (m >= 1000.0) {
      return std::format("{:.1f}B", m / 1000.0);
    } else if (m >= 100.0) {
      return std::format("{}M", (int)std::round(m));
    } else if (m > 10.0) {
      return std::format("{:.1f}M", m);
    } else {
      // TODO: Integer division. color decimal place and suffix.
      return std::format("{:.2f}M", m);
    }
  } else {
    return Util::UnsignedWithCommas(n);
  }
}


// (actually an upper bound, not inclusive)
static constexpr int MAX_FACES = 80;

struct Leaffast {

  static constexpr int METHOD =
    DB::METHOD_CONSTRUCT;
  // DB::METHOD_RANDOM_SYMMETRIC;

  static constexpr int SAMPLES_PER_THREAD = 16384;
  static constexpr int NUM_THREADS = 8;

  ArcFour main_rc;

  static constexpr int SAMPLE_LINE = 0;
  StatusBar status = StatusBar(3);

  double time_sample = 0.0;
  double time_solve = 0.0;

  Leaffast() : main_rc(std::format("leaffast.{}", time(nullptr))) {

  }

  ~Leaffast() {

  }

  // TODO: Get sample, loop over all face/edge pairs, but only
  // keep it if we find a face/edge pair with zero numerator.

  Polyhedron Sample(ArcFour *rc, int method) {
    switch (METHOD) {

    case DB::METHOD_RANDOM_CYCLIC: {
      const int num_verts = 8 + RandTo(rc, 54);
      return Sampler::RandomCyclicPolyhedron(rc, num_verts);
    }

    case DB::METHOD_RANDOM_SYMMETRIC: {
      const int num_verts = 8 + RandTo(rc, 54);
      return Sampler::RandomSymmetricPolyhedron(rc, num_verts, MAX_FACES);
    }

#if 0
    case DB::METHOD_OPT: {
      return Sampler::OptSample(&status, rc);
    }
#endif

    case DB::METHOD_CONSTRUCT: {
      return Sampler::MakeConstruct(&status, rc, MAX_FACES);
    }

    default:
      LOG(FATAL) << "Bad method?";
    }
  }

  std::vector<int64_t> best_denom = std::vector<int64_t>(MAX_FACES, 1);

  void Run() {
    DB db;

    Periodically status_per(1.0);
    Periodically histo_per(10.0);
    Periodically flush_per(59.0, false);
    Timer timer;

    const int64_t seed = Rand64(&main_rc);

    std::mutex m;
    status.Print("Begin parallel...\n");
    fflush(stdout);
    ParallelFan(
        NUM_THREADS,
        [&](int thread_idx) {
          ArcFour rc(std::format("{}.{}", seed, thread_idx));
          status.Print("Started thread {}.\n", thread_idx);
          fflush(stdout);

          for (;;) {

            Timer sample_timer;
            Aug aug(Sample(&rc, METHOD));
            ctr_poly++;
            const double sample_sec = sample_timer.Seconds();

            const Polyhedron &poly = aug.poly;
            const int num_faces = poly.faces->NumFaces();
            const int num_edges = poly.faces->NumEdges();
            [[maybe_unused]]
            const int num_verts = poly.faces->NumVertices();

            const int64_t already_denom = [&]{
                MutexLock ml(&m);
                return best_denom[num_faces];
              }();

            // Try to find an edge/face pair that isn't solvable.
            Timer solve_timer;
            for (int e = 0; e < num_edges; e++) {
              const Faces::Edge &edge = poly.faces->edges[e];
              for (int f : {edge.f0, edge.f1}) {

                // Require higher standard as we get a larger
                // number of faces. Also require beating our previous
                // record in this process so that we don't flood
                // the database.
                const int64_t MAX_SAMPLES =
                  std::max(already_denom + 1,
                           (int64_t)(131072 * sqrt(num_faces)));

                const bool is_hard = [&]{
                    for (int s = 0; s < MAX_SAMPLES; s++) {
                      BitString unfolding = SolveLeaf::SampleLeaf(
                          &rc, aug, f, e);

                      if (Albrecht::IsNet(aug, unfolding)) {
                        ctr_samples += s;
                        return false;
                      }
                    }

                    ctr_samples += MAX_SAMPLES;
                    return true;
                  }();

                const double solve_sec = solve_timer.Seconds();
                if (is_hard) {
                  time_sample += sample_sec;
                  time_solve += solve_sec;
                  // Write to database.
                  const DB::Why why = {DB::LeafIH{
                      .edge_idx = e,
                      .face_idx = f,
                    }};
                  // This approach inherently has netness 0, and
                  // no example.
                  db.AddHard(poly, why, METHOD, 0, MAX_SAMPLES,
                             std::nullopt);

                  ctr_saved++;
                  goto next;
                }
              }
            }

            // Then we solved it for all!
            ctr_satisfied_leaf++;

          next:;

            status_per.RunIf([&]{
                double total_time = timer.Seconds();
                double sample_pct = (time_sample * 100.0) / total_time;
                double solve_pct = (time_solve * 100.0) / total_time;

                // First line reserved for subprocess
                status.LineStatus(1,
                                  "{}\n",
                                  Sampler::SampleStats());
                status.LineStatus(
                    2,
                    "{} polys, {} samples, {} leafy, {} saved. "
                    "{} ({:.1f}% + {:.1f}%) \n",
                    FormatNum(ctr_poly.Read()),
                    FormatNum(ctr_samples.Read()),
                    ctr_satisfied_leaf.Read(),
                    ctr_saved.Read(),
                    ANSI::Time(total_time),
                    sample_pct, solve_pct);
              });
          }
        });
  }

};

int main(int argc, char **argv) {
  ANSI::Init();

  printf("Started...\n");
  fflush(stdout);

  {
    Leaffast leaffast;
    printf("Created...\n");
    fflush(stdout);
    leaffast.Run();
  }

  return 0;
}
