
#include "albrecht.h"

#include <array>
#include <cstdlib>
#include <ctime>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "bit-string.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "status-bar.h"
#include "util.h"

#include <algorithm>
#include <map>

#include "base/print.h"
#include "periodically.h"

static std::string BriefMethodName(int method) {
  std::string_view s = DB::MethodName(method);
  Util::TryStripPrefix("METHOD_", &s);
  return std::string(s);
}

void Scoreboard() {
  DB db;
  std::vector<DB::Hard> hards = db.AllHard(false);

  struct Entry {
    int id = 0;
    int method = 0;
    double netness_pct = 0.0;
    int64_t numer = 0;
    int64_t denom = 0;
  };

  std::map<int, std::vector<Entry>> by_faces;

  StatusBar status(1);
  Periodically status_per(1);

  for (int i = 0; i < (int)hards.size(); i++) {
    const DB::Hard &h = hards[i];
    if (h.netness_denom == 0) continue;

    int nfaces = 0;
    if (std::optional<Polyhedron> opoly =
            PolyhedronFromConvexVertices(h.poly_points)) {
      nfaces = opoly.value().faces->NumFaces();
    }

    if (nfaces > 0) {
      Entry e;
      e.id = h.id;
      e.method = h.method;
      e.numer = h.netness_numer;
      e.denom = h.netness_denom;
      e.netness_pct = (h.netness_numer * 100.0) / h.netness_denom;
      by_faces[nfaces].push_back(e);
    }

    status_per.RunIf([&] {
      status.Progress(i + 1, hards.size(), "Evaluating");
    });
  }
  status.Remove();

  for (auto &pair : by_faces) {
    int nfaces = pair.first;
    std::vector<Entry> &entries = pair.second;

    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) {
                if (a.netness_pct != b.netness_pct) {
                  return a.netness_pct < b.netness_pct;
                }
                return a.id < b.id;
              });

    Print("\n" AWHITE("--- {} Faces ---") "\n", nfaces);
    int limit = std::min((int)entries.size(), 5);
    for (int i = 0; i < limit; i++) {
      const Entry &e = entries[i];
      Print(" #" ACYAN("{}") "  " AYELLOW("{}/{} ({:.3f}%)")
            "  Method: " AGREEN("{}") "\n",
            e.id, e.numer, e.denom, e.netness_pct,
            BriefMethodName(e.method));
    }
  }
}



int main(int argc, char **argv) {
  ANSI::Init();

  Scoreboard();

  return 0;
}
