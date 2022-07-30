
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

static void Render(const Exp *e, string filename) {
  static constexpr int SIZE = 1920;
  static constexpr int NUM_THREADS = 12;
  ImageRGBA img(SIZE, SIZE);

  static constexpr float XMIN = -2.3f, XMAX = 1.3f;
  static constexpr float YMIN = -1.8f, YMAX = 1.8f;
  static constexpr float WIDTH = XMAX - XMIN;
  static constexpr float HEIGHT = YMAX - YMIN;

  Timer timer;
  printf("Rendering " ABLUE("%d") AYELLOW("x") ABLUE("%d") "...\n",
         SIZE, SIZE);
  ParallelComp2D(
      SIZE, SIZE,
      [&img, e](int yp, int xp) {
        if (xp == 0 && (yp % 16) == 0) {
          printf("Row " ABLUE("%d") "/" ABLUE("%d") "\n", yp, SIZE);
        }

        float x = (xp / (float)SIZE) * WIDTH + XMIN;
        float y = ((SIZE - yp) / (float)SIZE) * HEIGHT + YMIN;

        hcomplex z((half)0, (half)0);
        hcomplex c((half)x, (half)y);

        #if 1
          // magnitude mode
          static constexpr int MAX_ITERS = 32;
          for (int i = 0; i < MAX_ITERS; i++) {
            z = EvaluateComplex(e, z) * c;
            // z = z * z + c;
          }

          float f = z.Abs() / 2.0f;
          uint32 color = ColorUtil::LinearGradient32(
              ColorUtil::HEATED_METAL, f);
          img.SetPixel32(xp, yp, color);
          return;

        #else
          // escape mode
          for (int i = 0; i < MAX_ITERS; i++) {
            // XXX need to know the actual escape
            if (z.Abs() > (half)2) {
              // Escaped. The number of iterations gives the pixel.
              float f = i / (float)MAX_ITERS;
              uint32 color = ColorUtil::LinearGradient32(
                  ColorUtil::HEATED_METAL, f);
              img.SetPixel32(xp, yp, color);
              return;
            }

            z = EvaluateComplex(e, z) * c;
            // z = z * z + c;
          }
          // Possibly in the set.
          img.SetPixel32(xp, yp, 0xFFFFFFFF);
        #endif

      },
      NUM_THREADS);
  double sec = timer.Seconds();
  printf("Took " ACYAN("%.3f") " sec (" APURPLE("%.2f") " p/s)\n",
         sec, (SIZE * SIZE) / sec);
  img.Save(filename);
  printf("Wrote " AYELLOW("%s") ".\n", filename.c_str());
}

static void Frac1() {
  Exp::Allocator alloc;
  string se = Util::ReadFile("perm16good2/converted.txt");
  string err;
  const Exp *e = Exp::Deserialize(&alloc, se, &err);
  CHECK(e) << err;
  Render(e, "frac1.png");
}

int main(int argc, char **argv) {
  AnsiInit();
  Frac1();

  printf(AGREEN("OK") "\n");
  return 0;
}
