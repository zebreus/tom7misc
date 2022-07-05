
#include <cstdint>
#include <vector>
#include <array>
#include <string>

#include "expression.h"
#include "opt/optimizer.h"
#include "grad-util.h"
#include "timer.h"
#include "image.h"
#include "color-util.h"

// Attempt to mimic a function with a "linear" expression
// (within some range).

using Table = Exp::Table;

using uint32 = uint32_t;
using uint16 = uint16_t;
using uint8 = uint8_t;

using namespace std;
using half_float::half;
using namespace half_float::literal;
using Allocator = Exp::Allocator;

static inline half GetHalf(uint16 u) {
  half h;
  static_assert(sizeof (h) == sizeof (u));
  memcpy((void*)&h, (void*)&u, sizeof (u));
  return h;
}

static inline uint16 GetU16(half h) {
  uint16 u;
  static_assert(sizeof (h) == sizeof (u));
  memcpy((void*)&u, (void*)&h, sizeof (u));
  return u;
}

static Table MakeTableFromFn(const std::function<half(half)> &f) {
  Table table;
  for (int i = 0; i < 65536; i++) {
    half x = GetHalf((uint16)i);
    half y = f(x);
    table[i] = GetU16(y);
  }
  return table;
}

double Error(half low, half high,
             const Table &target,
             const Table &f) {

  double total_error = 0.0;
  double total_width = high - low;
  for (half pos = low; pos < high; /* in loop */) {
    half next = nextafter(pos, high);
    uint16 upos = GetU16(pos);
    /*
    uint16 unext = GetU16(next);
    printf("%.6f -> %.6f = %04x -> %04x\n", (float)pos, (float)next,
           upos, unext);
    */
    half ytarget = GetHalf(target[upos]);
    half yf = GetHalf(f[upos]);

    double err = yf - ytarget;
    double width = (double)next - (double)pos;

    total_error += fabs(err) * width;

    pos = next;
  }

  return total_error / total_width;
}

static const Exp *Op1Exp(Allocator *alloc,
                         double c1, int c2, double c3, double c4) {
  return
    alloc->PlusC(
        alloc->TimesC(
            alloc->TimesC(
                // x + c1
                alloc->PlusC(alloc->Var(), GetU16((half)c1)),
                // * 0.999 ...
                0x3bffu,
                c2),
            GetU16((half)c3)),
        GetU16((half)c4));
}

// Operation 1.
// Optimize this 4-parameter function:
//
// (x + c1) * 0.999 * ... * 0.999 * c3 - c4
//            `---- c2 times ---'
using Op1Optimizer = Optimizer<1, 3, uint8>;

static Op1Optimizer::function_type
Op1Function(half low, half high, const Table &target) {
  return Op1Optimizer::function_type(
      [low, high, &target](Op1Optimizer::arg_type arg) ->
      Op1Optimizer::return_type {
        const int c2 = arg.first[0];
        const auto &[c1, c3, c4] = arg.second;

        static constexpr bool VERBOSE = false;

        Timer eval_timer;
        if (VERBOSE)
          printf("Call with [%.3f, %d, %.3f, %.3f]\n",
                 c1, c2, c3, c4);

        Allocator alloc;
        const Exp *exp = Op1Exp(&alloc, c1, c2, c3, c4);

        Timer tab_timer;
        Table table = Exp::TabulateExpressionIn(exp, low, high);
        double tab_sec = tab_timer.Seconds();
        if (VERBOSE)
          printf("  .. tab in %.3fs\n", tab_sec);

        Timer err_timer;
        double error = Error(low, high, target, table);
        double err_sec = err_timer.Seconds();

        if (VERBOSE)
          printf(" .. %.3fs [%.3f + %.3f]. error %.6f\n",
                 eval_timer.Seconds(), tab_sec, err_sec, error);

        return std::make_pair(error, std::make_optional('o'));
      });
}


// Try to approximate the target table within the range [low, high].
static const Exp *MakeFn(Allocator *alloc,
                         half low, half high,
                         const Table &target) {

  // We try to make progress by finding a series of operations that
  // reduces the error.

  Timer opt_timer;
  Op1Optimizer optimizer(Op1Function(low, high, target));
  optimizer.Run(
      {make_pair(100, 600)},
      {make_pair(-18.0, +18.0),
       make_pair(-18.0, +18.0),
       make_pair(-18.0, +18.0)},
      nullopt,
      nullopt,
      // seconds
      {60.0},
      nullopt);

  double run_sec = opt_timer.Seconds();
  printf("Ran %lld evals in %.3fs [%.3f/sec]\n",
         optimizer.NumEvaluations(), run_sec,
         optimizer.NumEvaluations() / run_sec);

  const auto argo = optimizer.GetBest();
  CHECK(argo.has_value());
  const auto &[arg, err, out_] = argo.value();
  const int c2 = arg.first[0];
  const auto &[c1, c3, c4] = arg.second;
  printf("Best: %.4f %d %.4f %.4f\n"
         "Error: %.5f\n", c1, c2, c3, c4, err);

  const Exp *exp = Op1Exp(alloc, c1, c2, c3, c4);

  return exp;
}

static Table DiffTable(const Table &a, const Table &b) {
  Table ret;
  for (int x = 0; x < 65536; x++) {
    half ya = GetHalf(a[x]);
    half yb = GetHalf(b[x]);

    ret[x] = GetU16(ya - yb);
  }
  return ret;
}

static constexpr ColorUtil::Gradient GREEN_BLUE {
  GradRGB(0.0f,  0x00FF00),
  GradRGB(1.0f,  0x0000FF),
};

static const Exp *MakeLoop(Allocator *alloc,
                           half low, half high, const Table &target) {
  // We build up the expression.
  const Exp *exp = alloc->TimesC(alloc->Var(), 0x0000);
  // Loop invariant: table represents the exp.
  Table table = Exp::TabulateExpression(exp);

  static constexpr int IMG_SIZE = 1920;

  ImageRGBA all_img(IMG_SIZE, IMG_SIZE);
  all_img.Clear32(0x000000FF);
  GradUtil::Grid(&all_img);
  GradUtil::Graph(target, 0xFFFFFF88, &all_img);

  static constexpr int ITERS = 100;
  for (int i = 0; i < ITERS; i++) {
    double error = Error(low, high, target, table);
    printf("Iter %d, error: %.6f\n", i, error);
    ImageRGBA img(IMG_SIZE, IMG_SIZE);
    img.Clear32(0x000000FF);
    GradUtil::Grid(&img);
    GradUtil::Graph(target, 0xFFFFFF88, &img);
    GradUtil::Graph(table, 0x33FF33AA, &img);

    {
      float f = i / (float)(ITERS - 1);
      uint32 color = ColorUtil::LinearGradient32(
          GradUtil::GREEN_BLUE, f) & 0xFFFFFF33;

      GradUtil::Graph(table, color, &all_img);
      all_img.Save("sine-all.png");
    }

    // so if we have e(x) = target(x) - table(x)
    Table err = DiffTable(target, table);
    // .. and we approximate error
    const Exp *err_exp = MakeFn(alloc, low, high, err);
    // then f(x) = table(x) + err(x)
    // should improve our approximation.
    exp = alloc->PlusE(exp, err_exp);

    {
      Table err_approx = Exp::TabulateExpression(err_exp);
      GradUtil::Graph(err, 0xFFFF00AA, &img);
      GradUtil::Graph(err_approx, 0xFF0000AA, &img);
      img.BlendText32(2, 2, 0xFFFFFFAA,
                      StringPrintf("Iter %d, error: %.6f", i, error));
      img.Save(StringPrintf("sine%d.png", i));
    }

    // PERF: This can be done in place.
    table = Exp::TabulateExpression(exp);
  }

  return exp;
}

int main(int argc, char **argv) {
  Table sine =
    MakeTableFromFn([](half x) {
        return sin(x * (half)3.141592653589);
      });

  #if 0
  Table best = MakeFn((half)-1.0, (half)+1.0, sine);

  ImageRGBA img(2048, 2048);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);
  GradUtil::Graph(sine, 0xFFFFFF88, &img);
  GradUtil::Graph(best, 0x33FF33AA, &img);
  img.Save("sine.png");
  #endif

  Allocator alloc;
  [[maybe_unused]]
  const Exp *e = MakeLoop(&alloc,
                          (half)-1.0, (half)+1.0, sine);

  {
    std::string s = Exp::ExpString(e);
    Util::WriteFile("expression.txt", s);
  }

  return 0;
}
