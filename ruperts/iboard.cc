
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "bignum/big.h"
#include "hypercube.h"
#include "patches.h"
#include "periodically.h"
#include "status-bar.h"
#include "timer.h"
#include "util.h"
#include "threadutil.h"

using Volume = Hypercube::Volume;

static StatusBar status(37);

static constexpr int NUM_PATCHES = 36;

#define ABLOOD(s) AFGCOLOR(148, 0, 0, s)

namespace {
struct Scoreboard {
  struct Score {
    bool done = false;
    double vol_outscope = 0.0;
    double vol_inscope = 0.0;
    double vol_proved = 0.0;
  };

  Score &At(int outer, int inner) {
    CHECK(outer >= 0 && outer < NUM_PATCHES);
    CHECK(inner >= 0 && inner < NUM_PATCHES);

    return scores[outer * NUM_PATCHES + inner];
  }

  const Score &At(int outer, int inner) const {
    CHECK(outer >= 0 && outer < NUM_PATCHES);
    CHECK(inner >= 0 && inner < NUM_PATCHES);

    return scores[outer * NUM_PATCHES + inner];
  }

  Scoreboard() : scores(NUM_PATCHES * NUM_PATCHES) {}
 private:
  std::vector<Score> scores;
};
}

std::string ScoreboardString(const Scoreboard &scoreboard) {
  std::string out;
  for (int y = 0; y < NUM_PATCHES; y++) {
    for (int x = 0; x < NUM_PATCHES; x++) {
      const Scoreboard::Score &score = scoreboard.At(y, x);
      double vol_done = score.vol_outscope + score.vol_proved;
      if (score.done) {
        out.append(AGREEN("▉"));
      } else {
        if (vol_done > 0.0) {
          out.append(AYELLOW("░"));
        } else {
          if (x == y) {
            out.append(ABLOOD("∙"));
          } else {
            out.append(AGREY("∙"));
          }
        }
      }
    }
    out.append("\n");
  }
  return out;
}

static void IBoard() {
  PatchInfo patch_info = LoadPatchInfo("scube-patchinfo.txt");

  BigRat big_pi(165707065, 52746197);
  Volume bounds;

  // Test this out with a single pair to start.
  std::vector<std::pair<uint64_t, PatchInfo::CanonicalPatch>> canonical;
  for (const auto &[cc, p] : patch_info.canonical) {
    canonical.emplace_back(cc, p);
  }
  std::sort(canonical.begin(), canonical.end(),
            [](const auto &a,
               const auto &b) {
              return a.first < b.first;
            });
  CHECK(canonical.size() >= 2);
  CHECK(canonical.size() == NUM_PATCHES);

  const double total_volume =
    Hypercube::Hypervolume(Hypercube::MakeStandardBounds()).ToDouble();

  Scoreboard scoreboard;

  int done = 0;
  Periodically status_per(2);
  // Careful running everything in parallel because these can take gigabytes
  // of RAM.
  ParallelComp2D(
      canonical.size(), canonical.size(),
      [&](int outer_idx, int inner_idx) {
        const PatchInfo::CanonicalPatch &outer = canonical[outer_idx].second;
        const PatchInfo::CanonicalPatch &inner = canonical[inner_idx].second;
        std::unique_ptr<Hypercube> hypercube(new Hypercube);

        std::string filename = Hypercube::StandardFilename(
            outer.code, inner.code);
        if (!Util::ExistsFile(filename))
          return;

        Timer load_timer;
        std::string contents = Util::ReadFile(filename);
        if (Hypercube::IsComplete(contents)) {
          double complete_time = load_timer.Seconds();
          Scoreboard::Score &score = scoreboard.At(outer_idx, inner_idx);
          score.done = true;
          done++;
          status.Print("Noted {} is done in {}.", filename,
                       ANSI::Time(complete_time));

        } else {

          status.Print("Loading " AWHITE("{}") "...", filename);
          hypercube->FromString(contents);
          status.Print("Loaded {} in {}.", filename,
                       ANSI::Time(load_timer.Seconds()));

          // XXX HERE: Report stats, verify, etc.

          // PERF: Don't need to actually get the leaves in order
          // to compute these.
          Timer leaf_timer;
          double vol_outscope = 0.0, vol_proved = 0.0;
          auto leaves = hypercube->GetLeaves(&vol_outscope, &vol_proved);
          double vol_done = vol_outscope + vol_proved;
          double vol_inscope = total_volume - vol_outscope;
          status.Print(
              "{} leaves. {:.6f} ({:.3f}%) done, {:.6f} ({:.3f}%) proved. ({})",
              leaves.size(),
              vol_done,
              (vol_done * 100.0) / total_volume,
              vol_proved,
              (vol_proved * 100.0) / vol_inscope,
              ANSI::Time(leaf_timer.Seconds()));

          Scoreboard::Score &score = scoreboard.At(outer_idx, inner_idx);
          score.done = leaves.empty();
          score.vol_inscope = vol_inscope;
          score.vol_outscope = vol_outscope;
          score.vol_proved = vol_proved;
          if (score.done) done++;
        }

        status_per.RunIf([&]() {
            status.EmitStatus(
                std::format(
                    "{}"
                    "{}/{} processed. {} ({:.3f}%) all done.\n",
                    ScoreboardString(scoreboard),
                    outer_idx * NUM_PATCHES + inner_idx,
                    NUM_PATCHES * NUM_PATCHES,
                    done,
                    (done * 100.0) / (NUM_PATCHES * NUM_PATCHES)));
          });
      },
      4);

  printf("Scoreboard:\n%s\n",
         ScoreboardString(scoreboard).c_str());
}


int main(int argc, char **argv) {
  ANSI::Init();

  IBoard();

  return 0;
}
