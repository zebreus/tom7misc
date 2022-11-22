
#include "pactom.h"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "base/logging.h"
#include "geom/latlon.h"
#include "bounds.h"
#include "lines.h"
#include "util.h"
#include "ansi.h"

using namespace std;

static constexpr double METERS_TO_FEET = 3.28084;

int main(int argc, char **argv) {
  AnsiInit();

  #define TOMDIR "d:\\oldtom\\runs"
  vector<string> raw_tomdir = Util::ListFiles(TOMDIR);
  vector<string> tomdir;
  for (string &f : raw_tomdir) {
    if (f != "CVS" && f.find("~") == string::npos &&
        !Util::StartsWith(f, "3dwr")) {
      tomdir.push_back(Util::dirplus(TOMDIR, f));
    }
  }
  printf("%d runs in tomdir\n", tomdir.size());

  unique_ptr<PacTom> pactom = PacTom::FromFiles(tomdir, "../neighborhoods.kml");
  CHECK(pactom.get() != nullptr);

  #if 0
  unique_ptr<PacTom> pactom = PacTom::FromFiles({"../pac.kml",
                                                 "../pac2.kml"},
    "../neighborhoods.kml"
    );
  CHECK(pactom.get() != nullptr);
  #endif

  int has_date = 0;
  double path_feet = 0.0, tripath_feet = 0.0;
  for (int ridx = 0; ridx < pactom->runs.size(); ridx++) {
    const auto &run = pactom->runs[ridx];
    double pf = 0.0, tf = 0.0;
    for (int i = 0; i < run.path.size() - 1; i++) {
      const auto &[latlon0, elev0] = run.path[i];
      const auto &[latlon1, elev1] = run.path[i + 1];
      double dist1 = LatLon::DistFeet(latlon0, latlon1);
      pf += dist1;
      double dz = (elev1 - elev0) * METERS_TO_FEET;

      tf += sqrt(dz * dz + dist1 * dist1);
    }

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
           pf / 5280.0, tf / 5280.0);

    path_feet += pf;
    tripath_feet += tf;
  }
  printf("Total miles: %.6f\n", path_feet / 5280.0);
  printf("Including elev: %.6f\n", tripath_feet / 5280.0);
  printf("%d/%d have dates\n", has_date, pactom->runs.size());

}
