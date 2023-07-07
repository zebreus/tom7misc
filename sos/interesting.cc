
#include <vector>
#include <string>

#include "util.h"
#include "sos-util.h"
#include "ansi.h"
#include "re2/re2.h"

#include "base/logging.h"

using namespace std;

using re2::RE2;

static void Interesting() {
  //     (!) 115147526400 274233600 165486240000 143974713600 93636000000 43297286400 21785760000 186997766400 72124473600
  RE2 almost2("\\(!\\) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+)");

  int64_t num_almost2 = 0, num_almost1 = 0;
  std::vector<std::string> lines = Util::ReadFileToLines("interesting.txt");
  for (const std::string &line : lines) {
    int64_t aa, bb, cc, dd, ee, ff, gg, hh, ii;
    if (RE2::FullMatch(line, almost2, &aa, &bb, &cc, &dd, &ee, &ff, &gg, &hh, &ii)) {
      int64_t a = Sqrt64(aa);
      int64_t b = Sqrt64(bb);
      int64_t c = Sqrt64(cc);
      int64_t d = Sqrt64(dd);
      int64_t e = Sqrt64(ee);
      int64_t f = Sqrt64(ff);
      int64_t g = Sqrt64(gg);
      int64_t h = Sqrt64(hh);
      int64_t i = Sqrt64(ii);

      CHECK(b * b == bb);
      CHECK(c * c == cc);
      CHECK(d * d == dd);
      CHECK(e * e == ee);
      CHECK(f * f == ff);
      CHECK(g * g == gg);
      CHECK(i * i == ii);
      num_almost2++;

      CHECK(h * h != hh);

      if (a * a == aa) num_almost1++;
    }
  }

  printf("%lld almost2, %lld almost1\n", num_almost2, num_almost1);
}

int main(int argc, char **argv) {
  ANSI::Init();

  Interesting();

  return 0;
}
