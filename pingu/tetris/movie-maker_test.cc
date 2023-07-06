
#include "movie-maker.h"

#include <cstdint>
#include <string>
#include <vector>
#include <cstdio>
#include <unordered_map>

#include "threadutil.h"
#include "arcfour.h"
#include "randutil.h"
#include "timer.h"
#include "image.h"

#ifdef __MINGW32__
#include <windows.h>
#endif

using namespace std;
using uint64 = uint64_t;

#define ANSI_RED "\x1B[1;31;40m"
#define ANSI_GREY "\x1B[1;30;40m"
#define ANSI_BLUE "\x1B[1;34;40m"
#define ANSI_CYAN "\x1B[1;36;40m"
#define ANSI_YELLOW "\x1B[1;33;40m"
#define ANSI_GREEN "\x1B[1;32;40m"
#define ANSI_WHITE "\x1B[1;37;40m"
#define ANSI_PURPLE "\x1B[1;35;40m"
#define ANSI_RESET "\x1B[m"

static void CPrintf(const char* format, ...) {
  // Do formatting.
  va_list ap;
  va_start(ap, format);
  string result;
  StringAppendV(&result, format, ap);
  va_end(ap);

  #ifdef __MINGW32__
  DWORD n = 0;
  WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE),
               result.c_str(),
               result.size(),
               &n,
               nullptr);
  #else
  printf("%s", result.c_str());
  #endif
}


static constexpr int NUM_THREADS = 4;
static constexpr int NUM_BYTES = 10;



namespace {
// From pluginvert/modelinfo, with modifications. Probably should be
// in cc-lib?
struct Histo {

  Histo(const std::unordered_map<int, int> &values) : values(values) {}
  
  // Assumes width is the number of buckets you want.
  // If tallest_bucket is, say, 0.9, the bars are stretched to go 90%
  // of the way to the top of the image (1.0 is a sensible default but
  // can be confusing in the presence of tick marks, say).
  //
  // The lbound and ubound can be given explicitly (and then these
  // are the returned values as well), but typically they are derived
  // from the data itself.
  std::tuple<float, float, ImageA> MakeImage(
      int width, int height,
      double tallest_bucket = 1.0,
      optional<float> lbound = nullopt,
      optional<float> ubound = nullopt) const {
    ImageA img(width, height);

    float lo = std::numeric_limits<float>::infinity();
    float hi = -std::numeric_limits<float>::infinity();

    for (const auto &[v, count_] : values) {
      lo = std::min((float)v, lo);
      hi = std::max((float)v, hi);
    }

    if (lbound.has_value()) lo = lbound.value();
    if (ubound.has_value()) hi = ubound.value();    
    
    const float ival = hi - lo;
    const float bucket_width = ival / width;
    const float oval = 1.0f / ival;
    if (ival <= 0) return make_tuple(lo, hi, img);

    vector<int64> count(width, 0);
    for (const auto &[v, c] : values) {
      float f = (v - lo) * oval;
      int bucket = std::clamp((int)roundf(f * (width - 1)), 0, width - 1);
      count[bucket] += c;
    }

    int64 max_count = 0;
    int maxi = 0;
    for (int i = 0; i < (int)count.size(); i++) {
      int64 c = count[i];
      if (c > max_count) {
        max_count = c;
        maxi = i;
      }
    }

    if (max_count <= 0)
      return make_tuple(lo, hi, img);
    
    // Finally, fill in the image.
    for (int bucket = 0; bucket < width; bucket++) {
      CHECK(bucket >= 0 && bucket < (int)count.size());
      double hfrac = count[bucket] / (double)max_count;
      float fh = (hfrac * tallest_bucket) * (height - 1);
      int h = fh;
      float fpart = fh - h;
      // don't allow zero pixels.
      // this is not accurate but I want to be able to see
      // non-empty buckets clearly
      if (h == 0 && count[bucket] > 0) {
        h = 1;
        fpart = 0.0f;
      }
      int nh = height - h;
      if (nh > 0) {
        uint8 v = roundf(fpart * 255);
        img.SetPixel(bucket, nh - 1, v);
      }
      for (int y = nh; y < height; y++) {
        CHECK(bucket < img.Width() && bucket >= 0 &&
              y < img.Height() && y >= 0) << "height: " << height <<
          " nh: " << nh <<
          " bucket: " << bucket << " " << y;
        img.SetPixel(bucket, y, 0xFF);
      }
    }

    // Label the mode.
    float center = lo + ((maxi + 0.5f) * bucket_width);
    string label = StringPrintf("%.4f", center);
    int lw = label.size() * 9;
    // Align on left or right of the label so as not to run off the screen
    // (we could also try to avoid other buckets?)
    int x = maxi > (width / 2) ? maxi - (lw + 2) : maxi + 3;
    // Align with the peak, taking into account tallest_bucket.
    int y = (1.0 - tallest_bucket) * (height - 1);
    img.BlendText(x, y, 0xFF, label);

    return make_tuple(lo, hi, img);
  }

  // For example with tick=0.25, vertical lines at -0.25, 0, 0.25, 0.50, ...
  static ImageRGBA TickImage(int width, int height, float lo, float hi,
                             uint32 negative_tick_color,
                             uint32 zero_tick_color,
                             uint32 positive_tick_color,
                             float tick) {
    ImageRGBA img(width, height);
    const float ival = hi - lo;
    const float bucket_width = ival / width;

    for (int x = 0; x < width; x++) {
      const float bucket_lo = lo + x * bucket_width;
      const float bucket_hi = bucket_lo + bucket_width;
      // Does any tick edge reside in the bucket?
      // (Note there can be more than one...)

      // Floor here because we need rounding towards negative
      // infinity, not zero.
      const int tlo = floorf(bucket_lo / tick);
      const int thi = floorf(bucket_hi / tick);
      if (tlo != thi) {
        uint32 tick_color = 0;
        // tlo and thi are floor, so zero would fall in the bucket
        // [-1, 0]
        if (tlo == -1 && thi == 0) tick_color = zero_tick_color;
        else if (tlo < 0) tick_color = negative_tick_color;
        else tick_color = positive_tick_color;
        for (int y = 0; y < height; y++) {
          img.SetPixel32(x, y, tick_color);
        }
      }
    }
    return img;
  }

  const std::unordered_map<int, int> &values;
};
}  // namespace

static string VecBytes(const std::vector<uint8> &v) {
  string out;
  for (uint8 b : v) {
    StringAppendF(&out, "%02x", b);
  }
  return out;
}

// Returns movie size, num steps executed.
static std::pair<int, int> RunOne(uint64 run_seed) {
  ArcFour rc(StringPrintf("run.%llx", run_seed));

  const int64_t mm_seed = RandTo(&rc, 1000000000);
  vector<uint8> encode;
  encode.reserve(NUM_BYTES);
  for (int i = 0; i < NUM_BYTES; i++) encode.push_back(rc.Byte());

  Timer one_timer;
  MovieMaker mm("best-solutions.txt", "tetris.nes", mm_seed);

  CPrintf("Encode [" ANSI_YELLOW "%s" ANSI_RESET "] "
          "with seed [" ANSI_WHITE "%llx" ANSI_RESET "]\n",
          VecBytes(encode).c_str(), mm_seed);

  auto Stuck = [mm_seed, encode, &one_timer]() {
      return StringPrintf("Encoding [%s] with seed %llx, apparently stuck "
                          "after %.1fs", VecBytes(encode).c_str(), mm_seed,
                          one_timer.Seconds());
    };

  MovieMaker::Callbacks callbacks;
  callbacks.retried = [&one_timer, &Stuck](
      const Emulator &emu, int retry_count, Piece expected) {
      CHECK(retry_count < 10000) << Stuck();
      CHECK(one_timer.Seconds() < 60 * 10) << Stuck();
    };

  callbacks.made_step = [&one_timer, &Stuck](
      const Emulator &emu, int64 steps) {
      CHECK(steps < 4000000) << Stuck();
      CHECK(one_timer.Seconds() < 60 * 10) << Stuck();
    };

  vector<uint8> movie = mm.Play(encode, callbacks);

  CPrintf("Done: [" ANSI_YELLOW "%s" ANSI_RESET "] "
          "with seed [" ANSI_WHITE "%llx" ANSI_RESET "], "
          ANSI_PURPLE "%.2f" ANSI_WHITE "s"
          ANSI_RESET "\n",
          VecBytes(encode).c_str(), mm_seed, one_timer.Seconds());

  return make_pair((int)movie.size(), (int)mm.StepsExecuted());
}

static void RunForever() {

  ArcFour rc(StringPrintf("mmt.%lld", time(nullptr)));

  Asynchronously async(NUM_THREADS);

  Timer run_timer;
  static constexpr int REPORT_EVERY = 30;
  int64 next_report = 0;

  std::mutex m;
  int64 num_done = 0;
  std::unordered_map<int, int> steps_histo;
  std::unordered_map<int, int> movie_histo;

  auto Average = [](const std::unordered_map<int, int> &histo) {
      int64 sum = 0;
      int64 num = 0;
      for (const auto &[k, v] : histo) {
        sum += k * v;
        num += v;
      }
      return sum / (double)num;
    };
  
  // Holding mutex
  auto HistoImage = [](const std::unordered_map<int, int> &um,
                       const string &name,
                       int lb, int ub) {
      static constexpr int w = 512;
      static constexpr int h = 384;
      static constexpr int hmargin = 14;
      Histo histo(um);
      const auto &[lo, hi, himg] = histo.MakeImage(
          w, h - hmargin, 0.9, lb, ub);

      ImageRGBA color(himg.Width(), himg.Height() + 14);

      const ImageRGBA timg = Histo::TickImage(w, h, lo, hi,
                                              0xFF777735,
                                              0xFFFFFF75,
                                              0x77FF7735,
                                              1000.0f);
      color.BlendImage(0, 0, timg);
      color.BlendImage(0, 0, himg.AlphaMaskRGBA(0xFF, 0xFF, 0x00));

      const int label_y = (h - hmargin) + 1;
      color.BlendText32(0, label_y,
                        0xFFAAAAFF,
                        StringPrintf("%.9f", lo));
      const string his = StringPrintf("%.9f", hi);
      color.BlendText32(0 + w - (his.size() * 9), label_y,
                        0xAAFFAAFF,
                        his);

      color.BlendText32(0 + (w - (name.size() * 9)) / 2, label_y,
                        0xFFFFAAFF,
                        name);

      /*
      color.BlendText32(4, hcolor.Height() + 3, 0x999933FF,
                        StringPrintf("^ %s ^", name.c_str()));
      */
      return color;
    };

  // Holding mutex
  auto SaveImage = [&run_timer, &num_done,
                    &steps_histo, &movie_histo, &HistoImage]() {
      ImageRGBA simg = HistoImage(steps_histo, "steps", 0, 200000);
      ImageRGBA mimg = HistoImage(movie_histo, "movie", 0, 30000);
      ImageRGBA out(simg.Width(), simg.Height() + mimg.Height() + 18);
      out.BlendImage(0, 0, simg);
      out.BlendImage(0, simg.Height(), mimg);
      int ybot = simg.Height() + mimg.Height() + 4;
      double sec = run_timer.Seconds();
      out.BlendText32(4, ybot, 0x779933FF,
                      StringPrintf("%lld done in %.1fs (%.4f bytes/sec)",
                                   num_done, sec,
                                   (num_done * (double)NUM_BYTES) / sec));
      out.Save("movie-maker-test.png");
    };
  
  for (;;) {
    const uint64 seed = Rand64(&rc);
    async.Run([&m, &num_done, &steps_histo, &movie_histo, seed]() {
        const auto [movie_size, num_steps] = RunOne(seed);
        {
          MutexLock ml(&m);
          num_done++;
          movie_histo[movie_size]++;
          steps_histo[num_steps]++;
          // more stats?
        }
      });


    if (run_timer.Seconds() > next_report) {
      {
        MutexLock ml(&m);
        num_done++;
        double sec = run_timer.Seconds();
        double avg_steps = Average(steps_histo);
        double avg_movie = Average(movie_histo);
        CPrintf(ANSI_WHITE "%lld " ANSI_RESET " done in "
                ANSI_BLUE "%.2f" ANSI_WHITE "s"
                ANSI_RESET " = " ANSI_PURPLE "%.4f "
                ANSI_WHITE "bytes" ANSI_RESET "/" ANSI_WHITE "sec "
                ANSI_WHITE "%.1f " ANSI_RESET "movie "
                ANSI_WHITE "%.1f " ANSI_RESET "steps\n",
                num_done, sec, (num_done * (double)NUM_BYTES) / sec,
                avg_movie, avg_steps);
        SaveImage();
      }
      next_report = run_timer.Seconds() + REPORT_EVERY;
    }

  }
}


int main(int argc, char **argv) {
  #ifdef __MINGW32__
  if (!SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)) {
    LOG(FATAL) << "Unable to go to BELOW_NORMAL priority.\n";
  }

  HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
  // mingw headers may not know about this new flag
  static constexpr int kVirtualTerminalProcessing = 0x0004;
  DWORD old_mode = 0;
  GetConsoleMode(hStdOut, &old_mode);
  SetConsoleMode(hStdOut, old_mode | kVirtualTerminalProcessing);
  #endif

  RunForever();
  return 0;
}
