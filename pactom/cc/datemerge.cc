
#include "pactom.h"

#include <cstdio>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "base/logging.h"
#include "ansi.h"
#include "pactom-util.h"

using namespace std;

int main(int argc, char **argv) {
  ANSI::Init();

  std::unique_ptr<PacTom> pactom = PacTomUtil::Load(true);

  int has_date = 0;
  double path_miles = 0.0, tripath_miles = 0.0;
  for (int ridx = 0; ridx < pactom->runs.size(); ridx++) {
    const auto &run = pactom->runs[ridx];
    double pm = PacTom::RunMiles(run, false);
    double tm = PacTom::RunMiles(run, true);

    const char *DCOLOR = run.year == 0 ? ANSI_RED : ANSI_PURPLE;
    if (run.year > 0) has_date++;
    printf("%d" AGREY(".") " "
           "%s%04d" ANSI_RESET "-"
           "%s%02d" ANSI_RESET "-"
           "%s%02d" ANSI_RESET " "
           ABLUE("%s") AGREY(":") " "
           AYELLOW("%.3f") AGREY("/") AWHITE("%.3f") " "
           "mi.\n", ridx,
           DCOLOR, run.year, DCOLOR, run.month, DCOLOR, run.day,
           run.name.c_str(),
           pm, tm);

    path_miles += pm;
    tripath_miles += tm;
  }
  printf("Total miles: %.6f\n", path_miles);
  printf("Including elev: %.6f\n", tripath_miles);
  printf("%d/%d have dates\n", has_date, (int)pactom->runs.size());

  return 0;
}
