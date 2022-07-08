
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
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"

#include "makefn-ops.h"

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

template<class Op>
struct OpOptimizer {
  using Opt = Optimizer<Op::INT_ARGS, Op::DOUBLE_ARGS, uint8>;

  static Opt::function_type
  GetFunction(half low, half high, const Table &target) {
  return typename Opt::function_type(
      [low, high, &target](const Opt::arg_type &arg) ->
      Opt::return_type {
        const auto &[ints, dbls] = arg;
        Allocator alloc;
        const Exp *exp = Op::GetExp(&alloc, ints, dbls, target);
        Table table = Exp::TabulateExpressionIn(exp, low, high);
        double error = Error(low, high, target, table);
        return std::make_pair(error, std::make_optional('o'));
      });
  }
};


// Try to approximate the target table within the range [low, high].
static const Exp *MakeFn(Allocator *alloc,
                         half low, half high,
                         const Table &target,
                         double time_sec) {

  // We try to make progress by finding a series of operations that
  // reduces the error.

  // XXX make thread safe, etc. Probably don't want to use the
  // same sequence each time, either?
  static int64_t seed = 0x0BCDEF0012345600ULL;

  std::mutex m;
  std::vector<std::tuple<string, const Exp *, double>> results;

  const int64_t seed1 = seed++;
  const int64_t seed2 = seed++;
  const int64_t seed3 = seed++;
  const int64_t seed4 = seed++;

  InParallel(
      [alloc, low, high, &target, time_sec, &m, &results, seed1]() {
        Timer opt_timer;
        using Op1Optimizer = OpOptimizer<Op1>;
        Op1Optimizer::Opt optimizer(
            Op1Optimizer::GetFunction(low, high, target),
            seed1);
        optimizer.Run(
            Op1::INT_BOUNDS,
            Op1::DOUBLE_BOUNDS,
            nullopt,
            nullopt,
            // seconds
            {time_sec},
            nullopt);

        double run_sec = opt_timer.Seconds();

        const auto argo = optimizer.GetBest();
        CHECK(argo.has_value());
        const auto &[args, err, out_] = argo.value();
        const auto &[ints, dbls] = args;
        string desc = Op1::Describe(ints, dbls);

        printf("Op1 Ran %lld evals in %.3fs [%.3f/sec] err %.4f\n",
               optimizer.NumEvaluations(), run_sec,
               optimizer.NumEvaluations() / run_sec,
               err);

        const Exp *exp = Op1::GetExp(alloc, ints, dbls, target);

        {
          MutexLock ml(&m);
          results.emplace_back(desc, exp, err);
        }
      },
      [alloc, low, high, &target, time_sec, &m, &results, seed2]() {
        Timer opt_timer;
        using Op2Optimizer = OpOptimizer<Op2>;
        Op2Optimizer::Opt optimizer(
            Op2Optimizer::GetFunction(low, high, target),
            seed2);
        optimizer.Run(
            Op2::INT_BOUNDS,
            Op2::DOUBLE_BOUNDS,
            nullopt,
            nullopt,
            // seconds
            {time_sec},
            nullopt);

        double run_sec = opt_timer.Seconds();

        const auto argo = optimizer.GetBest();
        CHECK(argo.has_value());
        const auto &[args, err, out_] = argo.value();
        const auto &[ints, dbls] = args;
        string desc = Op2::Describe(ints, dbls);

        printf("Op2 Ran %lld evals in %.3fs [%.3f/sec] err %.4f\n",
               optimizer.NumEvaluations(), run_sec,
               optimizer.NumEvaluations() / run_sec,
               err);

        const Exp *exp = Op2::GetExp(alloc, ints, dbls, target);

        {
          MutexLock ml(&m);
          results.emplace_back(desc, exp, err);
        }
      },
      #if 0
      [alloc, low, high, &target, time_sec, &m, &results, seed3]() {
        Timer opt_timer;
        using Op3Optimizer = OpOptimizer<Op3>;
        Op3Optimizer::Opt optimizer(
            Op3Optimizer::GetFunction(low, high, target),
            seed3);
        optimizer.Run(
            Op3::INT_BOUNDS,
            Op3::DOUBLE_BOUNDS,
            nullopt,
            nullopt,
            // seconds
            {time_sec},
            nullopt);

        double run_sec = opt_timer.Seconds();

        const auto argo = optimizer.GetBest();
        CHECK(argo.has_value());
        const auto &[args, err, out_] = argo.value();
        const auto &[ints, dbls] = args;
        string desc = Op3::Describe(ints, dbls);

        printf("Op3 Ran %lld evals in %.3fs [%.3f/sec] err %.4f\n",
               optimizer.NumEvaluations(), run_sec,
               optimizer.NumEvaluations() / run_sec,
               err);

        const Exp *exp = Op3::GetExp(alloc, ints, dbls, target);

        {
          MutexLock ml(&m);
          results.emplace_back(desc, exp, err);
        }
      },
      #endif
      [alloc, low, high, &target, time_sec, &m, &results, seed4]() {
        Timer opt_timer;
        using Op4Optimizer = OpOptimizer<Op4>;
        Op4Optimizer::Opt optimizer(
            Op4Optimizer::GetFunction(low, high, target),
            seed4);
        optimizer.Run(
            Op4::INT_BOUNDS,
            Op4::DOUBLE_BOUNDS,
            nullopt,
            nullopt,
            // seconds
            {time_sec},
            nullopt);

        double run_sec = opt_timer.Seconds();

        const auto argo = optimizer.GetBest();
        CHECK(argo.has_value());
        const auto &[args, err, out_] = argo.value();
        const auto &[ints, dbls] = args;
        string desc = Op4::Describe(ints, dbls);

        printf("Op4 Ran %lld evals in %.3fs [%.3f/sec] err %.4f\n",
               optimizer.NumEvaluations(), run_sec,
               optimizer.NumEvaluations() / run_sec,
               err);

        const Exp *exp = Op4::GetExp(alloc, ints, dbls, target);

        {
          MutexLock ml(&m);
          results.emplace_back(desc, exp, err);
        }
      },
      [alloc, low, high, &target, time_sec, &m, &results, seed4]() {
        Timer opt_timer;
        using Op5Optimizer = OpOptimizer<Op5>;
        Op5Optimizer::Opt optimizer(
            Op5Optimizer::GetFunction(low, high, target),
            seed4);
        optimizer.Run(
            Op5::INT_BOUNDS,
            Op5::DOUBLE_BOUNDS,
            nullopt,
            nullopt,
            // seconds
            {time_sec},
            nullopt);

        double run_sec = opt_timer.Seconds();

        const auto argo = optimizer.GetBest();
        CHECK(argo.has_value());
        const auto &[args, err, out_] = argo.value();
        const auto &[ints, dbls] = args;
        string desc = Op5::Describe(ints, dbls);

        printf("Op5 Ran %lld evals in %.3fs [%.3f/sec] err %.4f\n",
               optimizer.NumEvaluations(), run_sec,
               optimizer.NumEvaluations() / run_sec,
               err);

        const Exp *exp = Op5::GetExp(alloc, ints, dbls, target);

        {
          MutexLock ml(&m);
          results.emplace_back(desc, exp, err);
        }
      }
  );

  double best_err = 1.0/0.0;
  int bestidx = 0;
  for (int i = 0; i < results.size(); i++) {
    const auto &[desc_, exp_, err] = results[i];
    if (err < best_err) {
      bestidx = i;
    }
  }

  const auto &[desc, exp, err] = results[bestidx];
  printf("Best %d: %s (Error %.5f)\n",
         bestidx, desc.c_str(), err);

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
  static constexpr int IMG_SIZE = 1920;
  static const char *ALLIMG_FILE = "perm-all.png";

  #if 1
  // We build up the expression.
  const Exp *exp = alloc->TimesC(alloc->Var(), 0x0000);
  static constexpr int START_ITER = 0;

  ImageRGBA all_img(IMG_SIZE, IMG_SIZE);
  all_img.Clear32(0x000000FF);
  GradUtil::Grid(&all_img);
  GradUtil::Graph(target, 0xFFFFFF88, &all_img);

  #else
  // Restart! Needs some manual work.

  #include "perm-restart.h"
  const Exp *exp = StartExp(alloc);

  std::unique_ptr<ImageRGBA> all_load(ImageRGBA::Load(ALLIMG_FILE));
  CHECK(all_load.get() != nullptr);
  CHECK(all_load->Width() == IMG_SIZE);
  CHECK(all_load->Height() == IMG_SIZE);
  ImageRGBA all_img = *all_load;
  all_load.reset();

  #endif

  // Loop invariant: table represents the exp and
  // error the current error relative to the target.
  Table table = Exp::TabulateExpression(exp);
  double error = Error(low, high, target, table);

  static constexpr int ITERS = 1000;
  for (int i = START_ITER; i < ITERS; i++) {
    {
      float f = i / (float)(ITERS - 1);
      uint32 color = ColorUtil::LinearGradient32(
          GradUtil::GREEN_BLUE, f) & 0xFFFFFF33;

      GradUtil::Graph(table, color, &all_img);
      all_img.Save(ALLIMG_FILE);
    }

    for (int tries = 1; /* in loop */; tries++) {
      printf("Iter %d, error: %.6f\n", i, error);
      ImageRGBA img(IMG_SIZE, IMG_SIZE);
      img.Clear32(0x000000FF);
      GradUtil::Grid(&img);
      GradUtil::Graph(target, 0xFFFFFF88, &img);
      GradUtil::Graph(table, 0x33FF33AA, &img);

      // so if we have e(x) = target(x) - table(x)
      Table err = DiffTable(target, table);
      // .. and we approximate error
      const Exp *err_exp = MakeFn(alloc, low, high, err,
                                  20.0 + 5 * tries);

      {
        Table err_approx = Exp::TabulateExpression(err_exp);
        GradUtil::Graph(err, 0xFFFF00AA, &img);
        GradUtil::Graph(err_approx, 0xFF0000AA, &img);
        img.BlendText32(2, 2, 0xFFFFFFAA,
                        StringPrintf("Iter %d, tries %d, error: %.6f",
                                     i, tries, error));
        img.Save(StringPrintf("perm%d.png", i));
      }

      // then f(x) = table(x) + err(x)
      // should improve our approximation.
      const Exp *exp_tmp = alloc->PlusE(exp, err_exp);
      Table table_tmp = Exp::TabulateExpression(exp_tmp);

      // but only keep these if the error dropped by a nontrivial
      // amount.
      static constexpr double MIN_ERROR_DROP = 1.0 / 1000.0;
      double error_tmp = Error(low, high, target, table_tmp);
      if (error - error_tmp >= MIN_ERROR_DROP) {
        // Keep.
        exp = exp_tmp;
        // PERF: This can be done in place.
        table = std::move(table_tmp);
        error = error_tmp;
        // Decrease time quanta when this happens?
        break;
      } else {
        // Increase time quanta when this happens?
        printf("Error %.5f -> %.5f. Retry.\n", error, error_tmp);
      }
    }


    {
      std::string s = Exp::ExpString(exp);
      Util::WriteFile("perm-best.txt", s);
    }
  }

  return exp;
}

static Table RandomPermutation(int num) {
  ArcFour rc("makefn");

  std::vector<uint8_t> permutation;
  for (int i = 0; i < num; i++)
    permutation.push_back(i);

  Shuffle(&rc, &permutation);

  return MakeTableFromFn([&permutation, num](half x) {
      // put x in [-1, 1];
      double pos = (double)(x + 1) * (num / 2.0);
      int off = std::clamp((int)floor(pos), 0, num - 1);
      double y = permutation[off];
      return (half)(y / (num / 2.0) - 1.0);
    });
}

int main(int argc, char **argv) {
  /*
  Table target =
    MakeTableFromFn([](half x) {
        return sin(x * (half)3.141592653589);
      });
  */
  Table target = RandomPermutation(16);

  Allocator alloc;
  [[maybe_unused]]
  const Exp *e = MakeLoop(&alloc,
                          (half)-1.0, (half)+1.0, target);

  {
    std::string s = Exp::ExpString(e);
    Util::WriteFile("expression-perm.txt", s);
  }

  return 0;
}
