
#include "hyphenation.h"

#include <cstdio>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "util.h"

#undef CHECK_EQ
#define CHECK_EQ(s1, s2) do { \
  const auto ss1 = s1;        \
  const auto ss2 = s2;        \
  CHECK(ss1 == ss2) << "s1:\n" << ss1 << "\nvs s2:\n" << ss2 << "\n"; \
} while (0)

#define TESTCASE(word, expected) do {                           \
    std::vector<std::string> vec = hyphenation.Hyphenate(word); \
    std::string result = Util::Join(vec, "-");                  \
    CHECK(result == expected) << "result: " << result;          \
  } while (0)

static void TestHyphenations() {
  Hyphenation hyphenation;

  // From the exceptions list.
  TESTCASE("associate", "as-so-ciate");

  // Examples from Franklin's thesis.
  TESTCASE("hyphenation", "hy-phen-ation");
  TESTCASE("typesetting", "type-set-ting");
  TESTCASE("supercalifragilisticexpialidocious",
           "su-per-cal-ifrag-ilis-tic-ex-pi-ali-do-cious");

  // Preserve capitalization.
  TESTCASE("Cornstarch", "Corn-starch");
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestHyphenations();

  printf("OK\n");
  return 0;
}
