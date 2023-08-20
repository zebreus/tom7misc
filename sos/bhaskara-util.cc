
#include "bhaskara-util.h"

#include <utility>
#include <vector>
#include <string>
#include <cstdio>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "bounds.h"
#include "color-util.h"
#include "image.h"

std::string LongNum(const BigInt &a) {
  std::string num = a.ToString();
  if (num.size() > 80) {
    static constexpr int SHOW_SIDE = 8;
    int skipped = num.size() - (SHOW_SIDE * 2);
    return StringPrintf("%s…(%d)…%s",
                        num.substr(0, SHOW_SIDE).c_str(),
                        skipped,
                        num.substr(num.size() - SHOW_SIDE,
                                   string::npos).c_str());
  } else {
    return num;
  }
}


using namespace std;
void MakeImages(int64_t iters,
                const string &base, int image_idx,
                const std::vector<std::pair<Triple, Triple>> &history) {
  enum Shape {
    DISC,
    CIRCLE,
    FILLSQUARE,
  };

  // xy plot
  {
    printf("x/y plot\n");
    Bounds bounds;
    bounds.Bound(0, 0);

    for (int x = 0; x < history.size(); x++) {
      const auto &[t1, t2] = history[x];
      bounds.Bound(MapBig(t1.a), MapBig(t2.a));
      bounds.Bound(MapBig(t1.b), MapBig(t2.b));
      bounds.Bound(MapBig(t1.k), MapBig(t2.k));
    }
    if (bounds.Empty()) {
      printf("Bounds empty.\n");
    } else {
      printf("Bounds: %.3f,%.3f -- %.3f,%.3f\n",
             bounds.MinX(), bounds.MinY(),
             bounds.MaxX(), bounds.MaxY());
    }


    const int WIDTH = 1900;
    const int HEIGHT = 1900;
    ImageRGBA img(WIDTH, HEIGHT);
    img.Clear32(0x000000FF);
    Bounds::Scaler scaler = bounds.Stretch(WIDTH, HEIGHT).FlipY();

    const auto [x0, y0] = scaler.Scale(0, 0);
    img.BlendLine32(x0, 0, x0, HEIGHT - 1, 0xFFFFF22);
    img.BlendLine32(0, y0, WIDTH - 1, y0, 0xFFFFF22);

    for (int i = 0; i < history.size(); i++) {
      const auto &[t1, t2] = history[i];

      auto Plot = [&img, &scaler](const BigInt &x, const BigInt &y,
                                  uint32_t color, Shape shape) {
          auto [sx, sy] = scaler.Scale(MapBig(x), MapBig(y));

          switch (shape) {
          case DISC:
            img.BlendFilledCircleAA32(sx, sy, 2.0f, color);
            break;
          case CIRCLE:
            img.BlendCircle32(sx, sy, 3, color);
            break;
          case FILLSQUARE:
            img.BlendRect32(sx - 1, sy - 1, 3, 3, color);
            break;
          }
        };

      Plot(t1.a, t2.a, 0xFF333355, DISC);
      Plot(t1.b, t2.b, 0x77FF7755, CIRCLE);
      Plot(t1.k, t2.k, 0x5555FF55, FILLSQUARE);
    }

    const int textx = 32;
    int texty = 32;
    img.BlendText2x32(textx + 18, texty, 0x555555FF,
                      StringPrintf("Iters: %lld", iters));
    texty += ImageRGBA::TEXT2X_HEIGHT + 2;
    img.BlendFilledCircleAA32(textx + 8, texty + 8, 6.0f, 0xFF3333AA);
    img.BlendText2x32(textx + 18, texty, 0xFF3333AA, "a");
    texty += ImageRGBA::TEXT2X_HEIGHT + 2;
    img.BlendCircle32(textx + 7, texty + 7, 5, 0x77FF77AA);
    img.BlendText2x32(textx + 18, texty, 0x77FF77AA, "b");
    texty += ImageRGBA::TEXT2X_HEIGHT + 2;
    img.BlendRect32(textx + 2, texty + 2, 11, 11, 0x5555FFAA);
    img.BlendText2x32(textx + 18, texty, 0x5555FFAA, "k");

    string filename = StringPrintf("%s-xyplot-%d.png",
                                   base.c_str(), image_idx);
    img.Save(filename);
    printf("Wrote " ACYAN("%s"), filename.c_str());
  }

  // seismograph
  {
    printf("seismograph\n");
    Bounds bounds;
    bounds.Bound(0, 0);

    auto BoundFiniteY = [&bounds](double d) {
        if (std::isfinite(d)) bounds.BoundY(d);
      };
    auto BoundTriple = [&BoundFiniteY](const Triple &triple) {
        BoundFiniteY(MapBig(triple.a));
        BoundFiniteY(MapBig(triple.b));
        BoundFiniteY(MapBig(triple.k));
      };

    for (int x = 0; x < history.size(); x++) {
      const auto &[t1, t2] = history[x];
      BoundTriple(t1);
      BoundTriple(t2);
    }
    bounds.BoundX(0);
    bounds.BoundX(history.size() - 1);

    constexpr int WIDTH = 1024 * 3;
    constexpr int HEIGHT = 1024;
    ImageRGBA img(WIDTH, HEIGHT);
    img.Clear32(0x000000FF);
    Bounds::Scaler scaler = bounds.Stretch(WIDTH, HEIGHT).FlipY();

    BigInt max_a{0}, max_b{0}, max_k{0};
    std::optional<BigInt> best_k = nullopt;

    for (int x = 0; x < history.size(); x++) {
      auto Plot = [x, &img, &scaler](
          const BigInt by, uint32_t rgb, Shape shape) {

          uint32_t color = (rgb << 8) | 0x80;

          double y = MapBig(by);
          int sx = scaler.ScaleX(x);
          int sy = 0;
          if (!std::isfinite(y)) {
            if (y > 0.0) sy = 0;
            else sy = HEIGHT - 1;
          } else {
            sy = scaler.ScaleY(y);
          }
          switch (shape) {
          case DISC:
            img.BlendFilledCircleAA32(sx, sy, 2.0f, color);
            break;
          case CIRCLE:
            img.BlendCircle32(sx, sy, 3, color);
            break;
          case FILLSQUARE:
            img.BlendRect32(sx - 1, sy - 1, 3, 3, color);
            break;
          }
        };

      const auto &[t1, t2] = history[x];
      if (t1.a > max_a) max_a = t1.a;
      if (t1.b > max_b) max_b = t1.b;
      if (BigInt::Abs(t1.k) > max_k) max_k = BigInt::Abs(t1.k);
      Plot(t1.a, 0xFF0000, CIRCLE);
      Plot(t1.b, 0xFF3300, CIRCLE);
      Plot(t1.k, 0xFF7700, CIRCLE);

      if (t2.a > max_a) max_a = t2.a;
      if (t2.b > max_b) max_b = t2.b;
      if (BigInt::Abs(t2.k) > max_k) max_k = BigInt::Abs(t2.k);
      Plot(t2.a, 0x0000FF, DISC);
      Plot(t2.b, 0x0033FF, DISC);
      Plot(t2.k, 0x0077FF, DISC);

      if (t1.a == t2.a) {
        BigInt totalk = BigInt::Abs(t1.k) + BigInt::Abs(t2.k);
        if (!best_k.has_value() ||
            totalk < best_k.value()) best_k = {std::move(totalk)};
      }
    }
    int texty = 0;
    img.BlendText32(4, texty, 0x555555FF,
                    StringPrintf("Iters: %lld", iters));
    texty += ImageRGBA::TEXT_HEIGHT + 1;
    img.BlendText32(4, texty, 0xFF0000FF,
                    StringPrintf("Max a: %s", max_a.ToString().c_str()));
    texty += ImageRGBA::TEXT_HEIGHT + 1;
    img.BlendText32(4, texty, 0xFFFF00FF,
                    StringPrintf("Max b: %s", max_b.ToString().c_str()));
    texty += ImageRGBA::TEXT_HEIGHT + 1;
    img.BlendText32(4, texty, 0x77FF00FF,
                    StringPrintf("Max k: %s", max_k.ToString().c_str()));
    texty += ImageRGBA::TEXT_HEIGHT + 1;
    img.BlendText32(4, texty, 0x77FF00FF,
                    StringPrintf("Best k: %s",
                                 best_k.has_value() ?
                                 best_k.value().ToString().c_str() :
                                 "(none)"));
    texty += ImageRGBA::TEXT_HEIGHT + 1;

    string filename = StringPrintf("%s-seismograph-%d.png",
                                   base.c_str(), image_idx);
    img.Save(filename);
    printf("Wrote " ACYAN("%s"), filename.c_str());
  }
}

