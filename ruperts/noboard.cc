
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "polyhedra.h"
#include "solutions.h"
#include "util.h"

using Solution = SolutionDB::Solution;
using Nopert = SolutionDB::Nopert;
using Attempt = SolutionDB::Attempt;
using NopertAttempt = SolutionDB::NopertAttempt;

#define ABLOOD(s) AFGCOLOR(148, 0, 0, s)

static void PrintAll() {
  SolutionDB db;

  int ok = 0;
  int smallest = 99999;
  std::vector<Solution> solutions = db.GetAllNopertSolutions();
  std::unordered_map<int, int> solved;
  for (const Solution &sol : solutions) {
    std::string_view poly = sol.polyhedron;
    CHECK(Util::TryStripPrefix("nopert_", &poly)) << sol.polyhedron;
    // Print("{}\n", std::string(poly));
    solved[atoi(std::string(poly).c_str())]++;
  }

  std::vector<Nopert> noperts = db.GetAllNoperts();

  std::vector<Attempt> attempts = db.GetAttemptsForNoperts();

  std::unordered_map<std::string, int64_t> attempt_count;
  for (const Attempt &att : attempts) {
    attempt_count[att.polyhedron] += att.iters;
  }

  std::sort(noperts.begin(), noperts.end(),
            [](const Nopert &a, const Nopert &b) {
              if (a.vertices.size() == b.vertices.size())
                return a.id < b.id;
              return a.vertices.size() < b.vertices.size();
            });

  for (const Nopert &nopert : noperts) {
    std::string method = SolutionDB::NopertMethodName(nopert.method);
    (void)Util::TryStripPrefix("NOPERT_METHOD_", &method);


    std::optional<Polyhedron> poly =
      PolyhedronFromConvexVertices(nopert.vertices,
                                   std::format("nopert_{}", nopert.id));

    std::string faces = ABLOOD("☠");
    if (poly.has_value()) {
      faces = std::format(AWHITE("{}") "f",
                          (int)poly.value().faces->v.size());
    }

    Print(AGREY("{: 3d}.") " " AWHITE("{:4d}") AGREY("v")
           " {}"
           " via " ACYAN("{}") " ({})",
           nopert.id,
           (int)nopert.vertices.size(),
           faces,
           method,
           Util::FormatTime("%Y-%m-%d %H:%M", nopert.createdate));

    std::string name = SolutionDB::NopertName(nopert.id);

    const auto ait = attempt_count.find(name);
    int64_t a = ait == attempt_count.end() ? 0 : ait->second;

    int nsol = solved[nopert.id];
    if (nsol > 0) {
      Print("  " ARED("✘") " solved");
      if (a > 0) {
        Print(" " AGREY("({})"), FormatNum(a));
      }
    } else {

      if (a > 0) {
        Print("  " AYELLOW("⚡") "{}", FormatNum(a));
      }

      ok++;
      smallest = std::min(smallest, (int)nopert.vertices.size());
    }

    Print("\n");
  }

  Print("\n" ABLUE("{}") " noperts; " AGREEN("{}") " not rejected.\n"
        "Smallest: " AWHITE("{}") " verts.\n",
        noperts.size(), ok,
        smallest);
}

static void PrintAttempts() {
  SolutionDB db;

  std::map<int, int64_t> attempts_by_method;
  std::map<int, int64_t> attempts_by_points;

  int64_t attempts_all = 0;
  std::vector<NopertAttempt> atts = db.GetAllNopertAttempts();
  for (const NopertAttempt &att : atts) {
    Print(AGREY("{: 3d}.") " " AWHITE("{}") AGREY("v") " via "
          ACYAN("{}") " " AYELLOW("{}") " attempts\n",
          att.id,
          att.points,
          SolutionDB::NopertMethodName(att.method),
          att.attempts);
    attempts_by_method[att.method] += att.attempts;
    attempts_by_points[att.points] += att.attempts;
    attempts_all += att.attempts;
  }

  Print("\n" AWHITE("By method") ":\n");
  for (const auto &[method, num] : attempts_by_method) {
    Print("  " ACYAN("{}") ": " AYELLOW("{}") "\n",
          SolutionDB::NopertMethodName(method), num);
  }

  Print("\n" AWHITE("By points") ":\n");
  for (const auto &[pts, num] : attempts_by_points) {
    Print("  " AWHITE("{}") ": " AYELLOW("{}") "\n",
          pts, num);
  }

  Print("\n" AWHITE("Total") ": {}\n", attempts_all);
}

int main(int argc, char **argv) {
  ANSI::Init();

  if (argc >= 2 && (std::string)argv[1] == "--attempts") {
    Print(AWHITE("Nopert attempts:") "\n");
    PrintAttempts();
  } else {
    Print(AWHITE("Noperts:") "\n");
    PrintAll();
  }

  return 0;
}
