
#include "re2-util.h"

#include <format>
#include <span>
#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "re2/re2.h"

static void TestMapReplacement() {
  RE2 re("{(\\w+)}");

  std::string res =
    RE2Util::MapReplacement("The {one} and the {other}.",
                            re,
                            [](auto match) {
                              CHECK(match.size() == 2);
                              return std::format("_{}_", match[1]);
                            });
  CHECK(res == "The _one_ and the _other_.") << res;
}


int main(int argc, char **argv) {
  ANSI::Init();

  TestMapReplacement();

  Print("OK\n");
  return 0;
}
