
#include "scope-exit.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void TestBasic() {
  int x = 0;
  {
    ScopeExit exit([&] { x++; });
    CHECK(x == 0);
  }
  CHECK(x == 1);
}

static void TestOrder() {
  int x = 1;
  {
    ScopeExit exit1([&] { x += 2; });
    ScopeExit exit2([&] { x *= 3; });
  }
  CHECK(x == 5);
}

static void TestValueCapture() {
  int x = 0;
  int y = 0;
  {
    ScopeExit exit([&, y] { x = y + 1; });
    y = 10;
  }
  CHECK(x == 1);
}

static int g_calls = 0;
static void IncCalls() { g_calls++; }

static void TestFunctionPointer() {
  g_calls = 0;
  {
    ScopeExit exit(&IncCalls);
    CHECK(g_calls == 0);
  }
  CHECK(g_calls == 1);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestBasic();
  TestOrder();
  TestValueCapture();
  TestFunctionPointer();

  Print("OK\n");
  return 0;
}
