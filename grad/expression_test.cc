
#include "expression.h"
#include "timer.h"

#include "half.h"

#include "grad-util.h"

#include <windows.h>

using Table = Exp::Table;

static void TestIter() {
  Exp::Allocator alloc;
  const Exp *f1 =
    alloc.TimesC(
        alloc.PlusC(
            alloc.TimesC(
                alloc.Var(),
                // * 0.999 ...
                0x3bffu,
                500),
            0x8000),
        0x3d4b);

  const Exp *f2 =
    alloc.TimesC(
        alloc.PlusC(
            alloc.TimesC(
                // x - 4
                alloc.PlusC(alloc.Var(), 0xc400),
                // * 0.999 ...
                0x3bffu,
                300),
            0x42d4),
        0x3c00);

  const Exp *f3 =
    alloc.TimesC(
        alloc.PlusE(
            f1,
            alloc.TimesC(
                alloc.PlusC(
                    alloc.TimesC(
                        alloc.PlusC(alloc.Var(), Exp::GetU16(0.67_h)),
                        0x3c01u,
                        200),
                    0xba4a),
                0xba45)),
        Exp::GetU16(10.0_h));

  const Exp *f4a =
    alloc.TimesC(
        alloc.PlusC(
            alloc.TimesC(
                // x - 4
                alloc.PlusC(alloc.Neg(alloc.Var()), 0xc200),
                // * 0.999 ...
                0x3bffu,
                200),
            0x42d4),
        0x3c00);

  const Exp *f4b =
    alloc.TimesC(
        alloc.PlusC(
            alloc.TimesC(
                // x - 4
                alloc.PlusC(alloc.Neg(alloc.Var()), 0xc300),
                // * 0.999 ...
                0x3bffu,
                300),
            0x42d4),
        0x3c00);

  const Exp *f4 =
    alloc.PlusE(f4a,
                alloc.Neg(f4b));

  // Deep one.
  const Exp *f5 = alloc.Var();
  for (int i = 0; i < 100000; i++)
    f5 = alloc.PlusC(f5, Exp::GetU16(1.0_h));

  // Pre-cache.
  (void)Exp::TabulateExpression(f3);

  Timer tab_timer;
  Table result1 = Exp::TabulateExpression(f1);
  Table result2 = Exp::TabulateExpression(f2);
  Table result3 = Exp::TabulateExpression(f3);
  Table result4 = Exp::TabulateExpression(f4);
  double sec = tab_timer.Seconds();

  {
    printf("Run deep:\n");
    Timer deep_timer;
    Table result = Exp::TabulateExpressionIn(f5, (half)-1.0, (half)1.0);
    printf("Evaluate deep in %.3f sec\n",
           deep_timer.Seconds());
  }

  for (const Exp *e : {f1, f2, f3, f4}) {
    Table before = Exp::TabulateExpression(e);
    string s = Exp::Serialize(e);
    string err;
    const Exp *ee = Exp::Deserialize(&alloc, s, &err);
    CHECK(ee) << err << "\n" << s;
    Table after = Exp::TabulateExpression(ee);
    CHECK(before == after);
  }

  printf("Tabulated functions in %.3fs\n", sec);
  ImageRGBA img(1024, 1024);
  img.Clear32(0x000000FF);
  GradUtil::Graph(result1, 0xFFFF7788, &img);
  GradUtil::Graph(result2, 0x77FFFF88, &img);
  GradUtil::Graph(result3, 0x7777FF88, &img);
  GradUtil::Graph(result4, 0xFF77FF88, &img);
  img.Save("expression-test.png");
}

static void TestStack() {
  ULONG_PTR lowLimit;
  ULONG_PTR highLimit;
  GetCurrentThreadStackLimits(&lowLimit, &highLimit);

  ptrdiff_t s = highLimit - lowLimit;

  printf("Stack bytes: %lld\n", (int64)s);
}

int main(int argc, char **argv) {
  TestStack();
  // (void)GradUtil::MakeTable2();
  TestIter();

  printf("OK\n");
  return 0;
}
