
#include <string>
#include <cstdint>

#include "expression.h"
#include "util.h"
#include "image.h"
#include "color-util.h"
#include "ansi.h"

#include "half.h"
#include "hcomplex.h"
#include "threadutil.h"
#include "timer.h"

using half_float::half;
using Table = Exp::Table;

using namespace std;

static hcomplex EvaluateComplex(const Exp *e, hcomplex z) {
  switch (e->type) {
  case VAR: return z;
  case PLUS_C: {
    hcomplex res = EvaluateComplex(e->a, z);
    half rhs = Exp::GetHalf(e->c);
    for (int i = 0; i < e->iters; i++)
      res += rhs;
    return res;
  }
  case TIMES_C: {
    hcomplex res = EvaluateComplex(e->a, z);

    if (e->iters > 10) {
      uint16 rp = Exp::GetU16(res.Re());
      uint16 ip = Exp::GetU16(res.Im());
      return hcomplex(
          Exp::GetHalf(Exp::CachedTimes(rp, e->c, e->iters)),
          Exp::GetHalf(Exp::CachedTimes(ip, e->c, e->iters)));
    } else {
      half rhs = Exp::GetHalf(e->c);
      for (int i = 0; i < e->iters; i++)
        res *= rhs;
      return res;
    }
  }
  case PLUS_E:
    return EvaluateComplex(e->a, z) +
      EvaluateComplex(e->b, z);
  default:
    CHECK(false) << "Unknown expression type";
    return hcomplex();
  }
}

static std::pair<Table, Table> TabulateComplex(const Exp *e) {
  Table rtable, itable;
  // We can just compute the real and complex parts at the
  // same time (a + ai) as they do not interact.
  for (int x = 0; x < 65536; x++) {
    hcomplex c(Exp::GetHalf(x), Exp::GetHalf(x));
    hcomplex z = EvaluateComplex(e, c);
    rtable[x] = Exp::GetU16(z.Re());
    itable[x] = Exp::GetU16(z.Im());
  }
  return make_pair(rtable, itable);
}

static hcomplex EvalWithTables(const Table &rtable,
                               const Table &itable,
                               hcomplex c) {
  return hcomplex(Exp::GetHalf(rtable[Exp::GetU16(c.Re())]),
                  Exp::GetHalf(itable[Exp::GetU16(c.Im())]));
}

enum Mode {
  F_TIMES_C,
  F_PLUS_C,
  Z_SQUARED_PLUS_C,
};

template<bool MAGNITUDE_MODE, Mode MODE, int MAX_ITERS>
static void Render(const std::pair<Table, Table> &tables,
                   string filebase) {
  // static constexpr int OVERSAMPLE = 8;
  // static constexpr int FRAMES = 10 * 60;

  // static constexpr int OVERSAMPLE = 4;
  // static constexpr int FRAMES = 100;

  // Render for paper
  static constexpr int OVERSAMPLE = 8;
  static constexpr int FRAMES = 2;

  static constexpr bool ADD_TEXT = false;

  const auto &[rtable, itable] = tables;

  const float XMIN0 = -2.3f, XMAX0 = 1.3f;
  const float YMIN0 = -1.8f, YMAX0 = 1.8f;

  /*
  const float XMIN0 = -1.5f, XMAX0 = 1.5f;
  const float YMIN0 = -1.5f, YMAX0 = 1.5f;
  */
  // const float XMIN0 = -1.0f, XMAX0 = 1.0f;
  // const float YMIN0 = -1.0f, YMAX0 = 1.0f;

  /*
  const float XMIN1 = 0.1375f, XMAX1 = 0.2625;
  const float YMIN1 = 0.4875f, YMAX1 = 0.51250f;
  */

  const float XMIN1 = -0.0125f, XMAX1 = 0.0125f;
  const float YMIN1 = -0.0125f, YMAX1 = 0.0125f;


  // No zoom
  // const float XMIN1 = XMIN0, XMAX1 = XMAX0;
  // const float YMIN1 = YMIN0, YMAX1 = YMAX0;


  static constexpr int FRAME_WIDTH = 1920 * OVERSAMPLE;
  static constexpr int FRAME_HEIGHT = 1080 * OVERSAMPLE;
  // Math is in terms of the centered square, but we also draw
  // pixels outside it.
  static constexpr int SIZE = std::min(FRAME_WIDTH, FRAME_HEIGHT);

  // size of overscan on one side
  static constexpr int XMARGIN = (FRAME_WIDTH - SIZE) >> 1;
  static constexpr int YMARGIN = (FRAME_HEIGHT - SIZE) >> 1;

  for (int frame = 0; frame < FRAMES; frame++) {

    static constexpr int NUM_THREADS = 12;
    ImageRGBA img(FRAME_WIDTH, FRAME_HEIGHT);
    img.Clear32(0x000000FF);

    const float t = frame / (float)(FRAMES);
    const float XMIN = std::lerp(XMIN0, XMIN1, t);
    const float XMAX = std::lerp(XMAX0, XMAX1, t);
    const float YMIN = std::lerp(YMIN0, YMIN1, t);
    const float YMAX = std::lerp(YMAX0, YMAX1, t);

    CHECK(std::isfinite(XMIN)) << XMIN;
    CHECK(std::isfinite(XMAX)) << XMAX;
    CHECK(std::isfinite(YMIN)) << YMIN;
    CHECK(std::isfinite(YMAX)) << YMAX;

    const float WIDTH = XMAX - XMIN;
    const float HEIGHT = YMAX - YMIN;

    Timer timer;
    printf("[" AYELLOW("%d") "/" AYELLOW("%d") "] "
           "Rendering " ABLUE("%d") AYELLOW("x") ABLUE("%d") "...\n",
           frame, FRAMES,
           FRAME_WIDTH, FRAME_HEIGHT);
    ParallelComp2D(
        FRAME_HEIGHT, FRAME_WIDTH,
        [XMIN, YMIN, WIDTH, HEIGHT, frame,
         &img, &rtable, &itable](int yp, int xp) {
          if (xp == 0 && (yp % 1024) == 0) {
            printf("[" AYELLOW("%d") "/" AYELLOW("%d") "] "
                   "Row " ABLUE("%d") "/" ABLUE("%d") "\n",
                   frame, FRAMES,
                   yp, SIZE);
          }


          const int xpos = xp - XMARGIN;
          const int ypos = yp - YMARGIN;

          float x = (xpos / (float)SIZE) * WIDTH + XMIN;
          float y = ((SIZE - ypos) / (float)SIZE) * HEIGHT + YMIN;

          hcomplex z((half)0, (half)0);
          hcomplex c((half)x, (half)y);

          if constexpr (MAGNITUDE_MODE) {
            // magnitude mode
            for (int i = 0; i < MAX_ITERS; i++) {
              if constexpr (MODE == F_TIMES_C) {
                z = EvalWithTables(rtable, itable, z) * c;
              } else if constexpr (MODE == F_PLUS_C) {
                z = EvalWithTables(rtable, itable, z) + c;
              } else if constexpr (MODE == Z_SQUARED_PLUS_C) {
                z = z * z + c;
              }
            }

            float f = z.Abs() / 1.0f;
            uint32 color = ColorUtil::LinearGradient32(
                ColorUtil::HEATED_METAL, f);
            img.SetPixel32(xp, yp, color);
            return;

          } else {
            // escape mode
            for (int i = 0; i < MAX_ITERS; i++) {
              // XXX need to know the actual escape
              if (z.Abs() > (half)2.0) {
                // Escaped. The number of iterations gives the pixel.
                float f = i / (float)MAX_ITERS;
                uint32 color = ColorUtil::LinearGradient32(
                    ColorUtil::HEATED_METAL, f);
                img.SetPixel32(xp, yp, color);
                return;
              }

              if constexpr (MODE == F_TIMES_C) {
                z = EvalWithTables(rtable, itable, z) * c;
              } else if constexpr (MODE == F_PLUS_C) {
                z = EvalWithTables(rtable, itable, z) + c;
              } else if constexpr (MODE == Z_SQUARED_PLUS_C) {
                z = z * z + c;
              }
            }
            // Possibly in the set.
            img.SetPixel32(xp, yp, 0xFFFFFFFF);
          }

        },
        NUM_THREADS);
    double sec = timer.Seconds();
    printf("Took " ACYAN("%.3f") " sec (" APURPLE("%.2f") " p/s)\n",
           sec, (SIZE * SIZE) / sec);

    ImageRGBA scaled = img.ScaleDownBy(OVERSAMPLE);

    if (ADD_TEXT) {
      scaled.BlendText32(XMARGIN, 2, 0xFFFFFFFF,
                         StringPrintf("min %.4f,%.4f", XMIN, YMIN));
      string maxx = StringPrintf("max %.4f,%.4f", XMAX, YMAX);
      scaled.BlendText32(scaled.Width() - XMARGIN - maxx.size() * 9 - 2,
                         scaled.Height() - 9 - 2, 0xFFFFFFFF,
                         maxx);
    }

    string filename = StringPrintf("%s%d.png", filebase.c_str(), frame);

    scaled.Save(filename);
    printf("Wrote " AYELLOW("%s") ".\n", filename.c_str());
  }
}

[[maybe_unused]]
static void Frac1() {
  Exp::Allocator alloc;
  string se = Util::ReadFile("perm16good2/converted.txt");
  string err;
  const Exp *e = Exp::Deserialize(&alloc, se, &err);
  CHECK(e) << err;

  uint16_t u = Exp::EvaluateOn(e, Exp::GetU16((half)1.0));
  uint16_t inv = Exp::GetU16((half)1.0 / Exp::GetHalf(u));
  printf("inv: %04x\n", inv);

  Render<true, F_TIMES_C, 256>(TabulateComplex(e), "frac1_");
}

[[maybe_unused]]
static void Frac2() {
  Exp::Allocator alloc;

  const Exp *e =
    alloc.TimesC(
        alloc.TimesC(
            alloc.Var(),
            // * 0.999 ...
            // 0x3bffu,
            // 1.0000000...1
            0x3c01u,
            500),
        0x3960);

  uint16_t u = Exp::EvaluateOn(e, Exp::GetU16((half)1.0));
  uint16_t inv = Exp::GetU16((half)1.0 / Exp::GetHalf(u));
  printf("inv: %04x\n", inv);

  // XXX these may not be the right params.
  Render<true, F_TIMES_C, 256>(TabulateComplex(e), "frac2_");
}

[[maybe_unused]]
static void SquareBrot() {
  Exp::Allocator alloc;
  string se = Util::ReadFile("perm-best.txt");
  string err;
  const Exp *e = Exp::Deserialize(&alloc, se, &err);
  CHECK(e) << err;
  Render<false, F_PLUS_C, 256>(TabulateComplex(e), "squarebrot_");
}

[[maybe_unused]]
static void Mandelbrot() {
  Exp::Allocator alloc;
  Render<false, Z_SQUARED_PLUS_C, 64>(
      // unused tables
      make_pair(Table(), Table()),
      "mandelbrot_");
}


int main(int argc, char **argv) {
  AnsiInit();
  // Frac1();
  SquareBrot();
  Mandelbrot();

  printf(AGREEN("OK") "\n");
  return 0;
}
