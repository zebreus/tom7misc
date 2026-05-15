
#include "albrecht.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <cstdint>

#include "ansi-image.h"
#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "color-util.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "image.h"
#include "netness.h"
#include "periodically.h"
#include "poly-util.h"
#include "randutil.h"
#include "sampler.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"

using Aug = Albrecht::AugmentedPoly;
using OneSample = Sampler::OneSample;

DECLARE_COUNTERS(ctr_poly, ctr_zero, ctr_only_net, ctr_saved);

// (actually an upper bound, not inclusive)
static constexpr int MAX_FACES = 80;

static constexpr ColorUtil::Gradient BLACKBODY{
  GradRGB(0.0f, 0x333333),
  GradRGB(0.2f, 0x7700BB),
  GradRGB(0.5f, 0xFF0000),
  GradRGB(0.8f, 0xFFFF00),
  GradRGB(1.0f, 0xFFFFFF)
};

struct Leaffast {

  static constexpr int METHOD =
    DB::METHOD_CONSTRUCT;
    // DB::METHOD_OPT;
    // DB::METHOD_RANDOM_SYMMETRIC;

  static constexpr int SAMPLES_PER_THREAD = 16384;
  static constexpr int NUM_THREADS = 8;

  ArcFour main_rc;

  std::vector<int> histo = std::vector<int>(MAX_FACES * 101, 0);
  int &HistoCell(int faces, int pct) {
    return histo[faces * 101 + pct];
  }

  std::string HistoFile() {
    return std::format("leaffast-{}-{}.histo", METHOD, MAX_FACES);
  }

  void SaveHisto() {
    std::string out;
    for (int i = 0; i < histo.size(); i++) {
      AppendFormat(&out, "{}\n", histo[i]);
    }
    Util::WriteFile(HistoFile(), out);
  }

  void LoadHisto() {
    std::string filename = HistoFile();
    Print("Try load {}\n", filename);
    std::vector<std::string> vals = Util::ReadFileToLines(filename);
    Print("Got {} lines\n", vals.size());
    if (vals.size() != MAX_FACES * 101) {
      status.Print("Invalid or missing histo file.\n");
      return;
    }

    for (int i = 0; i < vals.size(); i++) {
      histo[i] = Util::ParseInt64(vals[i], 0);
    }
    Print("Loaded {} values from histo.\n", vals.size());
  }

  static constexpr int SAMPLE_LINE = 0;
  StatusBar status = StatusBar(3);

  double time_sample = 0.0;
  double time_measure = 0.0;

  double best_netness = 1.0;
  Leaffast() : main_rc(std::format("leaffast.{}", time(nullptr))) {

  }

  ~Leaffast() {

  }

  // TODO: Get sample, loop over all face/edge pairs, but only
  // keep it if we find a face/edge pair with zero numerator.

  OneSample Sample(ArcFour *rc, int method) {
    switch (METHOD) {
    case DB::METHOD_RANDOM_CYCLIC: {
      const int num_verts = 8 + RandTo(rc, 54);
      Aug aug = Aug(Sampler::RandomCyclicPolyhedron(rc, num_verts));
      const auto &[numer, denom] =
        Netness::Compute(Rand64(rc), aug, 131072, 2, 1);
      return OneSample{.aug = std::move(aug), .numer = numer, .denom = denom};
    }

    case DB::METHOD_RANDOM_SYMMETRIC: {
      const int num_verts = 8 + RandTo(rc, 54);
      Aug aug = Aug(Sampler::RandomSymmetricPolyhedron(rc, num_verts,
                                                       MAX_FACES));
      const auto &[numer, denom] =
        Netness::Compute(Rand64(rc), aug, 131072, 2, 1);
      return OneSample{.aug = std::move(aug), .numer = numer, .denom = denom};
    }

    case DB::METHOD_OPT: {
      return Sampler::OptSample(&status, rc);
    }

    case DB::METHOD_CONSTRUCT: {
      return Sampler::ConstructSample(&status, rc, MAX_FACES);
    }

    default:
      LOG(FATAL) << "Bad method?";
    }
  }

  void Run() {
    DB db;

    LoadHisto();

    Periodically status_per(1.0);
    Periodically histo_per(10.0);
    Periodically flush_per(59.0, false);
    Timer timer;

    // One channel for each face count, since we want the whole pareto
    // frontier.
    std::vector<std::optional<std::tuple<Polyhedron, int, int64_t, int64_t>>>
      new_best(MAX_FACES, std::nullopt);

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

            OneSample sample = Sample(&rc, METHOD);

            const Polyhedron &poly = sample.aug.poly;
            const int nfaces = poly.faces->NumFaces();
            const int nedges = poly.faces->NumEdges();
            const int nverts = poly.faces->NumVertices();

            double netness =
              // Should always have positive denominator..!
              sample.denom == 0 ? 1.0 :
              (sample.numer / (double)sample.denom);
            ctr_poly++;
            if (sample.numer == 0) {
              ctr_zero++;
            }
            if (sample.numer == sample.denom) {
              ctr_only_net++;
            }

            {
              MutexLock ml(&m);

              time_sample += sample.sample_sec;
              time_measure += sample.measure_sec;

              int pct = std::clamp((int)std::round(100.0 * netness), 0, 100);
              HistoCell(nfaces, pct)++;

              [&]{
                for (int p = pct - 1; p >= 0; p--) {
                  if (HistoCell(nfaces, p) > 0) {
                    return;
                  }
                }

                // Then it must be the best in this channel.
                new_best[nfaces] = std::make_tuple(
                    poly, METHOD,
                    sample.numer, sample.denom);

              }();
            }

            {
              MutexLock ml(&m);
              if (netness < best_netness) {
                std::string wrote;
                if (netness < 0.0005) {
                  std::string filename =
                    std::format("brecht-{}-{:.5g}.stl", time(nullptr),
                                netness * 100.0);
                  wrote = std::format(" Wrote " ACYAN("{}"), filename);
                  SaveAsSTL(poly, filename);
                }
                status.Print(AGREEN("New best!")
                             " {} faces, {} edges, {} vert.{}\n",
                             nfaces, nedges, nverts, wrote);
                best_netness = netness;
              }
            }

            status_per.RunIf([&]{
                double total_time = timer.Seconds();
                double sample_pct = (time_sample * 100.0) / total_time;
                double measure_pct = (time_measure * 100.0) / total_time;

                // First line reserved for subprocess
                status.LineStatus(1,
                              "{}\n",
                              Sampler::SampleStats());
                status.LineStatus(
                    2,
                    "{} polys, {} zero, {} only, "
                    "best {:.7g}, {} ({:.1f}% + {:.1f}%) \n",
                    ctr_poly.Read(),
                    ctr_zero.Read(),
                    ctr_only_net.Read(),
                    best_netness,
                    ANSI::Time(total_time),
                    sample_pct, measure_pct);
              });

            histo_per.RunIf([&] {
                MutexLock ml(&m);
                int norm = 0;
                for (int n : histo)
                  norm = std::max(norm, n);
                if (norm > 0) {
                  ImageRGBA img(MAX_FACES, 101);
                  for (int y = 0; y <= 100; y++) {
                    for (int x = 0; x < MAX_FACES; x++) {
                      int count = HistoCell(x, 100 - y);
                      double f = count / (double)norm;
                      uint32_t c = count == 0
                        ? 0x000000FF
                        : ColorUtil::LinearGradient32(BLACKBODY, f);
                      img.SetPixel32(x, y, c);
                    }
                  }

                  // Crop out left column because there are no such
                  // polyhedra, and we want to fit in 80 columns.
                  img = img.Crop32(3, 0, img.Width() - 3, img.Height());

                  status.Print("{}\n", ANSIImage::HalfChar(img));
                }
              });

            flush_per.RunIf([&]{
                MutexLock ml(&m);
                SaveHisto();
                bool any = false;
                for (auto &ov : new_best) {
                  if (ov.has_value()) {
                    any = true;
                    const auto &[poly, method, numer, denom] = ov.value();
                    db.AddHard(poly, method, numer, denom);
                    ctr_saved++;
                  }
                  ov = std::nullopt;
                }
                if (any) {
                  status.Print("Saved to DB.");
                }
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
