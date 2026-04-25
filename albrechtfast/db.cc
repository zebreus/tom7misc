#include "db.h"

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "database.h"
#include "geom/polyhedra.h"
#include "util.h"

using frame3 = DB::frame3;

using Hard = DB::Hard;

DB::DB(const char *dbfile) {
  std::string f = dbfile;
  // XXX would be good if we could determine when this
  // has reached the root?
  for (int i = 0; i < 5; i++) {
    if (Util::ExistsFile(f)) {
      db = Database::Open(f);
      CHECK(db.get() != nullptr) << f;
      break;
    }

    f = std::format("../{}", f);
  }

  if (db.get() == nullptr) {
    // Otherwise create it here.
    db = Database::Open(dbfile);
    CHECK(db.get() != nullptr) << dbfile;
  }

  Init();
}

static std::string StringFromVec3s(std::span<const vec3> vs) {
  std::string ret;
  for (const vec3 &v : vs) {
    AppendFormat(&ret,
                 "{:.17g},{:.17g},{:.17g};",
                 v.x, v.y, v.z);
  }

  if (!ret.empty()) {
    CHECK(ret.back() == ';');
    ret.pop_back();
  }

  return ret;
}

static std::optional<std::vector<vec3>> Vec3sFromString(std::string_view s) {
  std::vector<std::string> parts = Util::Split(s, ';');
  std::vector<vec3> ret;
  ret.reserve(parts.size());
  for (std::string_view sv : parts) {
    std::vector<std::string> vparts = Util::Split(sv, ',');
    if (vparts.size() != 3) return std::nullopt;
    vec3 v;
    for (int i = 0; i < 3; i++) {
      if (std::optional<double> od = Util::ParseDoubleOpt(vparts[i])) {
        v[i] = od.value();
      } else {
        return std::nullopt;
      }
    }
    ret.push_back(v);
  }
  return {ret};
}

void DB::Init() {
  db->ExecuteAndPrint("create table "
                      "if not exists "
                      "hard ("
                      "id integer primary key, "
                      // as vec3s
                      "poly string not null, "
                      "method integer not null, "
                      "createdate integer not null, "
                      "netness_numer not null, "
                      "netness_denom not null "
                      ")");
}

// Expects a specific column order; see below.
static std::vector<DB::Hard> GetHardForQuery(
    std::unique_ptr<Database::Query> q) {
  std::vector<DB::Hard> ret;
  while (std::unique_ptr<Database::Row> r = q->NextRow()) {
    Hard hard;
    hard.id = r->GetInt(0);
    std::optional<std::vector<vec3>> pts = Vec3sFromString(r->GetString(1));
    if (!pts.has_value()) continue;
    hard.poly_points = std::move(pts.value());
    hard.method = r->GetInt(2);
    hard.createdate = r->GetInt(3);
    hard.netness_numer = r->GetInt(4);
    hard.netness_denom = r->GetInt(5);
    ret.push_back(std::move(hard));
  }
  return ret;
}


Hard DB::GetHard(int id) {
  std::vector<Hard> sols = GetHardForQuery(
    db->ExecuteString(
        std::format(
            "select "
            "id, poly, method, createdate, netness_numer, netness_denom "
            "from hard "
            "where id = {}", id)));
  CHECK(sols.size() == 1) << "Hard " << id << " not found";
  return sols[0];
}

void DB::AddHard(const Polyhedron &poly, int method,
                 int64_t netness_numer, int64_t netness_denom) {
  std::string polystring = StringFromVec3s(poly.vertices);
  db->ExecuteAndPrint(
      std::format(
          "insert into hard "
          "(poly, method, createdate, netness_numer, netness_denom) "
          "values ('{}', {}, {}, {}, {})",
          polystring,
          method,
          time(nullptr),
          netness_numer, netness_denom));
}

