
#include "html-entities.h"

#include <format>

#include "base/logging.h"
#include "ansi.h"
#include "base/print.h"

#define CHECK_HAS_EQ(opt, expected) do {        \
  auto e = (expected);                          \
  auto o = (opt);                               \
  CHECK(o.has_value()) << #opt;                 \
  CHECK_EQ(o.value(), e) << #opt <<             \
    "\nvs\n" << #expected << "\nwhich is\n" <<  \
    e << "\n";                                  \
  } while (0)

static void TestEnt() {
  CHECK_HAS_EQ(HTMLEntities::GetEntity("CirclePlus"), "⊕");
  CHECK_HAS_EQ(HTMLEntities::GetEntity("nsubE"), "\u2ac5\u0338");
  CHECK(!HTMLEntities::GetEntity("notent").has_value());
  CHECK(!HTMLEntities::GetEntity("circleplus").has_value());
  CHECK(!HTMLEntities::GetEntity("&circleplus").has_value());
  CHECK(!HTMLEntities::GetEntity("&circleplus;").has_value());
  CHECK(!HTMLEntities::GetEntity("circleplus;").has_value());
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestEnt();

  Print("OK\n");
  return 0;
}
