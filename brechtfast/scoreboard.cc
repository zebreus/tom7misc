
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "db.h"
#include "periodically.h"
#include "status-bar.h"
#include "util.h"

static std::string BriefMethodName(int method) {
  std::string_view s = DB::MethodName(method);
  Util::TryStripPrefix("METHOD_", &s);
  return std::string(s);
}

// This is a scoreboard showing the hardest instances
// grouped by face for why=any (can we find ANY net?).
static void Scoreboard() {
  StatusBar status(1);
  Periodically status_per(1, false);
  status.Status("Read database...");

  DB db;
  std::vector<DB::Hard> hards = db.AllHard(false);


  struct Entry {
    int id = 0;
    int method = 0;
    double netness_pct = 0.0;
    int64_t numer = 0;
    int64_t denom = 0;
    bool has_example = false;
  };

  std::map<int, std::vector<Entry>> by_faces;

  status.Status("Load entries...");
  for (int i = 0; i < (int)hards.size(); i++) {
    const DB::Hard &h = hards[i];
    if (h.netness_denom == 0) continue;
    // Only any-type instances.
    if (!std::holds_alternative<DB::Any>(h.why)) continue;

    Entry e;
    e.id = h.id;
    e.method = h.method;
    e.numer = h.netness_numer;
    e.denom = h.netness_denom;
    e.netness_pct = (h.netness_numer * 100.0) / h.netness_denom;
    e.has_example = h.example_net.has_value();
    by_faces[h.num_faces].push_back(e);
  }

  status.Status("Sort...");
  for (auto &[nfaces, entries] : by_faces) {
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) {
                if (a.netness_pct != b.netness_pct) {
                  return a.netness_pct < b.netness_pct;
                }
                return a.id < b.id;
              });
  }

  status.Remove();
  for (const auto &[nfaces, entries] : by_faces) {

    Print("\n" AWHITE("--- {} Faces ---") "\n", nfaces);
    int limit = std::min((int)entries.size(), 5);
    for (int i = 0; i < limit; i++) {
      const Entry &e = entries[i];
      Print(" #" ACYAN("{}") "{} " AYELLOW("{}/{} ({:.3f}%)")
            "  Method: " AGREEN("{}") "\n",
            e.id,
            e.has_example ? " " : ARED("?"),
            e.numer, e.denom, e.netness_pct,
            BriefMethodName(e.method));
    }
  }
}



int main(int argc, char **argv) {
  ANSI::Init();

  Scoreboard();

  return 0;
}
