
#include <memory>
#include <string>
#include <cstdio>

#include "pactom-util.h"
#include "pactom.h"
#include "util.h"
#include "base/stringprintf.h"
#include "base/logging.h"

using namespace std;

int main(int argc, char **argv) {

  std::unique_ptr<PacTom> pactom = PacTomUtil::Load(true);

  double total_miles = 0.0;
  for (const PacTom::Run &run : pactom->runs) {
    total_miles += PacTom::RunMiles(run);
  }
  printf("Total miles: %.5f\n", total_miles);

  std::map<int, int> by_year;
  {
    string out;
    for (const PacTom::Run &run : pactom->runs) {
      if (run.year > 0) {
        by_year[run.year]++;
        StringAppendF(&out, "%04d-%02d-%02d\t%.5f\n",
                      run.year, run.month, run.day,
                      PacTom::RunMiles(run, true));
      }
    }

    Util::WriteFile("distances.tsv", out);
  }

  for (const auto &[year, count] : by_year) {
    printf("%04d: %d\n", year, count);
  }

  return 0;
}
