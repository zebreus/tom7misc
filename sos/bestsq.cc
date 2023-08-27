
#include "sos-util.h"

#include <vector>
#include <string>
#include <cstdint>

#include "atomic-util.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "bounds.h"
#include "threadutil.h"
#include "periodically.h"
#include "ansi.h"
#include "timer.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "bhaskara-util.h"
#include "util.h"

using namespace std;

inline BigInt BigSqrtError(const BigInt &aa) {
  const auto [a1, err1] = BigInt::SqrtRem(aa);
  BigInt a2 = a1 + 1;
  BigInt aa2 = a2 * a2;
  // Abs is probably not necessary? (But cheap)
  BigInt err2 = BigInt::Abs(aa2 - aa);
  return std::min(err1, err2);
}


static constexpr int64_t CHUNK_SIZE = 1'000'000;

struct Err {
  uint64_t best_idx = ~0;
  BigInt min;
  BigInt max;

  void Update(uint64_t idx, const BigInt &v) {
    if (v < min || best_idx == ~0) {
      best_idx = idx;
      min = v;
    }

    if (v > max || best_idx == ~0) {
      max = v;
    }
  }
};

struct ChunkResult {
  Err a;
  Err h;
  Err tot;
};

DECLARE_COUNTERS(chunks_done, u0_, u00_, u1_, u2_, u3_, u4_, u5_);

static void Search(uint64_t start, uint64_t num) {
  CHECK(num % CHUNK_SIZE == 0);
  uint64_t num_chunks = num / CHUNK_SIZE;

  Periodically status_per(1.0);
  Timer run_timer;

  chunks_done.Reset();

  std::vector<ChunkResult> chunks(num_chunks);
  ParallelComp(
      num_chunks,
      [&](int64_t chunk) {
        ChunkResult res;
        for (int64_t i = 0; i < CHUNK_SIZE; i++) {
          uint64_t idx = start + chunk * CHUNK_SIZE + i;
          BigInt x(idx);
          BigInt xx = x * x;

          BigInt aa = xx * uint64_t{151 * 1471};
          BigInt hh = xx * uint64_t{137 * 2633};

          BigInt aerr = BigSqrtError(aa);
          BigInt herr = BigSqrtError(hh);
          BigInt tot = aerr + herr;

          res.a.Update(idx, aerr);
          res.h.Update(idx, herr);
          res.tot.Update(idx, tot);
        }
        chunks[chunk] = std::move(res);
        chunks_done++;

        status_per.RunIf([&](){
            printf(ANSI_UP
                   "%s\n",
                   ANSI::ProgressBar(chunks_done.Read(),
                                     num_chunks,
                                     "errs",
                                     run_timer.Seconds()).c_str());
          });
      },
      12);

  printf("\n\n\n");
  std::optional<BigInt> best_total;
  FILE *file = fopen("bestsq.txt", "ab");
  CHECK(file != nullptr);
  for (int i = 0; i < num_chunks; i++) {
    const ChunkResult &chunk = chunks[i];
#define TERM_A AFGCOLOR(180, 34, 34, "%s")
#define TERM_H AFGCOLOR(34, 180, 34, "%s")
#define TERM_T AFGCOLOR(34, 34, 180, "%s")

    const BigInt &tot = chunk.tot.min;

    if (chunk.tot.best_idx != 0 &&
        (!best_total.has_value() || tot < best_total.value())) {
      BigInt x{chunk.tot.best_idx};
      const bool save = tot <= BigInt{1'000'000'000};
      // n.b. the total min is not the sum of the two mins
      printf("New best at %s: (" TERM_A ", " TERM_H "): " TERM_T " %s\n",
             x.ToString().c_str(),
             chunk.a.min.ToString().c_str(),
             chunk.h.min.ToString().c_str(),
             tot.ToString().c_str(),
             save ? "  " APURPLE("*") : "");
      const BigInt xx = x * x;
      const BigInt aa = xx * uint64_t{151 * 1471};
      const BigInt hh = xx * uint64_t{137 * 2633};
      printf("(x^2 = %s. aa = %s. hh = %s)\n",
             xx.ToString().c_str(),
             aa.ToString().c_str(),
             hh.ToString().c_str());
      if (save) {
        fprintf(file, "BEST %s %s %s %s\n",
                x.ToString().c_str(),
                tot.ToString().c_str(),
                chunk.a.min.ToString().c_str(),
                chunk.h.min.ToString().c_str());
        fprintf(file, "SQUARE %s %s %s\n",
                xx.ToString().c_str(),
                aa.ToString().c_str(),
                hh.ToString().c_str());
      }
      best_total = tot;
    }
  }
  fprintf(file, "DONE %llu %llu %.3f\n",
          start, start + num, run_timer.Seconds());
  fclose(file);
  printf("\n\n\n");

  printf("Make image...\n");
  constexpr int WIDTH = 3840, HEIGHT = 2160;
  ImageRGBA img(WIDTH, HEIGHT);
  img.Clear32(0x000000FF);
  Bounds bounds;
  bounds.Bound(0, 0);

  auto Map = [](const BigInt &z) -> double {
      // return z.ToDouble();
      return BigInt::NaturalLog(z + 1);
    };

  Err all_a, all_h, all_tot;
  for (int x = 0; x < num_chunks; x++) {
    const ChunkResult &chunk = chunks[x];

    bounds.Bound(x, Map(chunk.a.min));
    bounds.Bound(x, Map(chunk.a.max));
    bounds.Bound(x, Map(chunk.h.min));
    bounds.Bound(x, Map(chunk.h.max));
    bounds.Bound(x, Map(chunk.tot.min));
    bounds.Bound(x, Map(chunk.tot.max));

    all_a.Update(x, chunk.a.min);
    all_a.Update(x, chunk.a.max);
    all_h.Update(x, chunk.h.min);
    all_h.Update(x, chunk.h.max);
    all_tot.Update(x, chunk.tot.min);
    all_tot.Update(x, chunk.tot.max);
  }

  Bounds::Scaler scaler = bounds.Stretch(WIDTH, HEIGHT).FlipY();

  Timer img_timer;

  for (int x = 0; x < num_chunks; x++) {
    const ChunkResult &chunk = chunks[x];
    double sx = scaler.ScaleX(x);

    auto DrawRange = [&](const Err &e, uint32_t c) {
        const auto sy0 = scaler.ScaleY(Map(e.min));
        const auto sy1 = scaler.ScaleY(Map(e.max));
        img.BlendLine32(sx, sy0, sx, sy1, c);
      };

    DrawRange(chunk.a, 0xFF222255);
    DrawRange(chunk.h, 0x22FF2255);
    DrawRange(chunk.tot, 0x2222FF55);

    // img.BlendFilledCircleAA32(sx, say, 2.5, 0xFF222255);
    // img.BlendFilledCircleAA32(sx, shy, 2.5, 0x22FF2233);
    // img.BlendFilledCircleAA32(sx, sty, 2.5, 0x2222FF55);

    if (x % 1024 == 0) {
      status_per.RunIf([&](){
          printf(ANSI_UP
                 "%s\n",
                 ANSI::ProgressBar(x,
                                   num_chunks,
                                   "img",
                                   img_timer.Seconds()).c_str());
        });
    }
  }

  int yy = 8;
  img.BlendText2x32(8, yy, 0xFF0000AA,
                    StringPrintf("a: %s-%s",
                                 LongNum(all_a.min).c_str(),
                                 LongNum(all_a.max).c_str()));
  yy += ImageRGBA::TEXT2X_HEIGHT + 2;
  img.BlendText2x32(8, yy, 0x00FF00AA,
                    StringPrintf("h: %s-%s",
                                 LongNum(all_h.min).c_str(),
                                 LongNum(all_h.max).c_str()));
  yy += ImageRGBA::TEXT2X_HEIGHT + 2;
  img.BlendText2x32(8, yy, 0x0000FFAA,
                    StringPrintf("tot: %s-%s",
                                 LongNum(all_tot.min).c_str(),
                                 LongNum(all_tot.max).c_str()));
  yy += ImageRGBA::TEXT2X_HEIGHT + 2;


  string filename = StringPrintf("bestsq%llu_%llum.png", start, num_chunks);
  img.Save(filename);
  printf("Wrote %s\n", filename.c_str());
}

static void Next() {
  Timer round_timer;
  string next = Util::ReadFile("bestsq-next.txt");
  uint64_t start = atoll(next.c_str());
  CHECK(start != 0);
  constexpr uint64_t BATCH_SIZE = 10'000'000'000;
  printf("Running " ABLUE("%s") "-" ACYAN("%s") "...\n",
         Util::UnsignedWithCommas(start).c_str(),
         Util::UnsignedWithCommas(start + BATCH_SIZE).c_str());
  Search(start, BATCH_SIZE);
  Util::WriteFile("bestsq-next.txt", StringPrintf("%llu\n",
                                                  start + BATCH_SIZE));
  printf("Did " ABLUE("%s") "-" ACYAN("%s") " in %s\n",
         Util::UnsignedWithCommas(start).c_str(),
         Util::UnsignedWithCommas(start + BATCH_SIZE).c_str(),
         ANSI::Time(round_timer.Seconds()).c_str());
}

int main(int argc, char **argv) {
  ANSI::Init();

  // start, num chunks (in millions)
  // Search(10'001'000'000'000, 3840);

  // Search(1, 100);

  // done
  // Search(0, 100'000'000'000);
  // Search(100'000'000'000, 100'000'000'000);
  // Search(200'000'000'000, 100'000'000'000);
  for (;;) {
    if (!Util::ReadFile("stop").empty()) {
      Util::remove("stop");
      printf(AFGCOLOR(255, 255, 255, ABGCOLOR(180, 0, 0, " STOP ")) "\n");
      break;
    }
    Next();
  }

  return 0;
}
