#include "inline-vector.h"

#include <cstdint>

#include "ansi.h"
#include "base/print.h"
#include "base/logging.h"

static void TestSizes() {
  InlineVector<uint8_t> iv1;
  CHECK(iv1.MAX_INLINE >= 48) << "This depends on compiler alignment so it isn't "
    "really guaranteed. But we should tweak something if we can't even inline "
    "a significant number of bytes.";

  InlineVector<uint8_t *> iv2;
  CHECK(iv2.MAX_INLINE >= 4) << "This depends on compiler alignment so it isn't "
    "really guaranteed. But we should tweak something if we can't even inline "
    "a few pointers.";
}

static void TestSimple() {
  InlineVector<int> v;
  Print("InlineVector<int> can inline {}\n", v.MAX_INLINE);

  CHECK(v.size() == 0);
  CHECK(v.empty());

  for (int i = 0; i < 24; i++) {
    CHECK((int)v.size() == i);
    v.push_back(i);
    CHECK(!v.empty());
    CHECK(v.back() == i);
    for (int j = 0; j < (int)v.size(); j++) {
      CHECK(v[j] == j) << "On iter " << i << ", "
        "index " << j << " is actually " << v[j];
    }
  }

  InlineVector<int> vv = v;
  CHECK(vv == v);

  vv.push_back(9);
  CHECK(vv != v);
  v.push_back(9);
  CHECK(v == vv);

  CHECK(vv.back() == v.back());

  vv.pop_back();
  v.pop_back();
  CHECK(vv == v);

  v.clear();
  CHECK(v.empty());
  CHECK(v.size() == 0);
  for (int i = 0; i < (int)vv.size(); i++) {
    v.push_back(vv[i]);
  }
  CHECK(v == vv);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestSizes();
  TestSimple();

  Print("OK\n");
  return 0;
}
