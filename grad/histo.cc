
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

#include "util.h"
#include "image.h"
#include "ansi.h"
#include "half.h"

#include "grad-util.h"

using namespace std;

// from modelinfo; probably should promote to cc-lib somehow
struct Histo {
  void Add(double f) {
    if (bound_low.has_value() && f < bound_low.value())
      f = bound_low.value();
    if (bound_high.has_value() && f > bound_high.value())
      f = bound_high.value();

    values.push_back(f);
  }

  Histo() {}
  Histo(optional<double> bound_low, optional<double> bound_high) :
    bound_low(bound_low), bound_high(bound_high) {}

  // Assumes width is the number of buckets you want.
  // If tallest_bucket is, say, 0.9, the bars are stretched to go 90%
  // of the way to the top of the image (1.0 is a sensible default but
  // can be confusing in the presence of tick marks, say).
  std::tuple<double, double, ImageA> MakeImage(
      int width, int height,
      double tallest_bucket = 1.0) const {
    printf("MakeImage %d %d\n", width, height);
    ImageA img(width, height);

    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();

    for (double v : values) {
      lo = std::min(v, lo);
      hi = std::max(v, hi);
    }

    const double ival = hi - lo;
    const double bucket_width = ival / width;
    const double oval = 1.0f / ival;
    if (ival <= 0) return make_tuple(lo, hi, img);

    vector<int64> count(width, 0);
    for (double v : values) {
      double f = (v - lo) * oval;
      int bucket = roundf(f * (width - 1));
      CHECK(bucket >= 0 && bucket <= count.size()) << bucket;
      count[bucket]++;
    }

    int64 max_count = 0;
    int maxi = 0;
    for (int i = 0; i < count.size(); i++) {
      int64 c = count[i];
      if (c > max_count) {
        max_count = c;
        maxi = i;
      }
    }

    // Finally, fill in the image.
    for (int bucket = 0; bucket < width; bucket++) {
      double hfrac = count[bucket] / (double)max_count;
      double fh = (hfrac * tallest_bucket) * (height - 1);
      int h = fh;
      double fpart = fh - h;
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
              y < img.Height() && y >= 0) << bucket << " " << y;
        img.SetPixel(bucket, y, 0xFF);
      }
    }

    // Label the mode.
    double center = lo + ((maxi + 0.5f) * bucket_width);
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
  static ImageRGBA TickImage(int width, int height, double lo, double hi,
                             uint32 negative_tick_color,
                             uint32 zero_tick_color,
                             uint32 positive_tick_color,
                             double tick) {
    ImageRGBA img(width, height);
    const double ival = hi - lo;
    const double bucket_width = ival / width;

    for (int x = 0; x < width; x++) {
      const double bucket_lo = lo + x * bucket_width;
      const double bucket_hi = bucket_lo + bucket_width;
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

  // If present, samples are clipped to these bounds.
  optional<double> bound_low = nullopt, bound_high = nullopt;

  vector<double> values;
};


ImageRGBA RenderHistogram(const GradUtil::Table &table,
                          int width, int height,
                          int bucket_width,
                          optional<double> lb, optional<double> ub) {

  CHECK(width % bucket_width == 0) << "bucket_width should divide width";
  const int num_buckets = width / bucket_width;

  Histo histo(lb, ub);
  for (uint16_t u : table) {
    half h = GradUtil::GetHalf(u);
    double d = (double)h;
    // Should find a way to plot infinite values?
    if (isfinite(d)) {
      histo.Add(d);
    }
  }

  constexpr int hmargin = 0;
  const auto [lo, hi, rhimg] = histo.MakeImage(num_buckets,
                                               height - hmargin, 0.95);

  ImageA himg = rhimg.ScaleBy(bucket_width, 1);
  ImageRGBA color(himg.Width(), himg.Height());

  // Always ticks at 0.1.
  const ImageRGBA timg = Histo::TickImage(width, height, lo, hi,
                                          0xFF777735,
                                          0xFFFFFF75,
                                          0x77FF7735,
                                          0.125f);
  color.BlendImage(0, 0, timg);

  // minor ticks if scale is very small?
  const ImageRGBA mtimg = Histo::TickImage(width, height, lo, hi,
                                           0xFF777720,
                                           0xFFFFFF40,
                                           0x77FF7720,
                                           0.03125f);
  color.BlendImage(0, 0, mtimg);

  color.BlendImage(0, 0, himg.AlphaMaskRGBA(0xFF, 0xFF, 0x00));

  return color;
}

static void Histos() {
  GradUtil::Table table = GradUtil::IdentityTable();
  ImageRGBA img = RenderHistogram(table, 1920, 1080, 2, {-256.0}, {256.0});
  img.Save("histo.png");
}

int main(int argc, char **argv) {
  AnsiInit();

  Histos();

  return 0;
}
