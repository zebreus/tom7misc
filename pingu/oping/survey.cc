
#include <unistd.h>

#include <chrono>
#include <span>
#include <string>
#include <vector>
#include <array>
#include <deque>

#include "base/stringprintf.h"
#include "base/logging.h"

#include "arcfour.h"
#include "randutil.h"
#include "image.h"
#include "geom/hilbert-curve.h"
#include "util.h"
#include "threadutil.h"
#include "timer.h"
#include "color-util.h"

using namespace std;

using int64 = int64_t;
static constexpr uint8_t TIMEOUT = 0;
static constexpr uint8_t WRONG_DATA = 255;

struct Stats {
  int64 total = 0;
  int64 has_response = 0;
  // Histogram over byte values.
  std::array<int64, 256> count;
  Stats() {
    count.fill(0);
  }
};

[[maybe_unused]]
static Stats ReadRaw(int octet_c) {
  string filename = StringPrintf("ping%d.dat", (int)octet_c);
  std::vector<uint8_t> pings = Util::ReadFileBytes(filename);
  if (pings.empty()) {
    LOG(INFO) << "Missing file " << filename;
    return Stats();
  }
  CHECK(pings.size() == 256 * 256 * 256) << "Incomplete/bad file "
                                         << filename;
  Stats stats;
  stats.total = 256 * 256 * 256;
  for (int i = 0; i < 256 * 256 * 256; i++) {
    uint8_t b = pings[i];
    stats.count[(int)b]++;
    if (b != TIMEOUT && b != WRONG_DATA) stats.has_response++;
  }
  return stats;
}

int main(int argc, char **v) {
  Timer run_timer;

  std::vector<Stats> statses =
    ParallelTabulate(256, ReadRaw, 8);

  ImageRGBA statcube(256, 256);
  statcube.Clear32(0x000000FF);

  // Norm is the maximum value in [1,253] (normal ping timeout).
  int64_t norm = 0;
  for (int y = 0; y < 256; y++) {
    for (int x = 1; x <= 253; x++) {
      norm = std::max(norm, statses[y].count[x]);
    }
  }
  
  for (int y = 0; y < 256; y++) {
    // We still norm the special columns, though they will probably
    // be white.
    for (int x = 0; x < 256; x++) {
      float f = statses[y].count[x] / (double)norm;
      uint32_t color = ColorUtil::LinearGradient32(
          ColorUtil::HEATED_METAL, f);
      statcube.SetPixel32(x, y, color);
    }
  }

  statcube.ScaleBy(3).Save("statcube.png");

  for (int c = 0; c < 256; c++) {
    if (statses[c].has_response < (256 * 256 * 256 * 0.08f)) {
      printf("%d: Only %lld responses (%.2f%%)\n",
             c,
             statses[c].has_response,
             (100.0 * statses[c].has_response) / (256.0 * 256.0 * 256.0));
    }
  }

  // XXX
  return 0;
  
  printf("Checking images...\n");
  ParallelComp(256, [](int octet_c) {
      std::string filename = StringPrintf("subnet.a.b.%d.d.png", octet_c);
      std::unique_ptr<ImageRGBA> img(ImageRGBA::Load(filename));
      CHECK(img.get() != nullptr) << filename;
      CHECK(img->Width() == 4096 && img->Height() == 4096) << filename;
    }, 8);

  printf("OK in %.1fs!\n", run_timer.Seconds());
  return 0;
}
