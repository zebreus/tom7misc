
#include "functional-set.h"

#include <cstdio>
#include <string>

#include "base/logging.h"

static void SimpleTest() {
  using FS = FunctionalSet<std::string>;
  const FS empty;
  CHECK(!empty.Contains("hi"));

  {
    const FS fm1 = empty.Insert("hi");
    CHECK(!empty.Contains("hi"));
    CHECK(fm1.Contains("hi"));
  }

  FS fs = empty;

  fs = fs.Insert("first");

  for (const std::string &s : {"a", "b", "c", "d", "e",
      "f", "g", "h", "i", "j", "k"}) {
    CHECK(!fs.Contains(s));
    fs = fs.Insert(s);
    CHECK(fs.Contains(s));
  }

  CHECK(fs.Contains("a"));
  CHECK(fs.Contains("d"));
  CHECK(fs.Contains("j"));

  CHECK(fs.Remove("a").Contains("d"));
  CHECK(fs.Remove("d").Contains("a"));
  CHECK(fs.Remove("a").Remove("a").Contains("j"));

  CHECK(!fs.Remove("j").Contains("j"));
  CHECK(!fs.Remove("j").Remove("d").Contains("j"));
  CHECK(fs.Remove("j").Insert("j").Contains("j"));
  CHECK(!fs.Remove("j").Remove("j").Contains("j"));

  fs = fs.Remove("a");
  CHECK(fs.Contains("first"));
  fs = fs.Remove("first");

  const auto &s = fs.Export();
  CHECK(s.contains("b"));
  CHECK(s.contains("i"));
  CHECK(s.contains("g"));
  CHECK(!s.contains("hi"));
  CHECK(!s.contains("a")) << "Removed!";
  CHECK(!s.contains("first")) << "Removed!";
}

int main(int argc, char **argv) {
  SimpleTest();

  printf("OK\n");
  return 0;
}
