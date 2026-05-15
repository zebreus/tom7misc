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
#include <variant>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "bit-string.h"
#include "database.h"
#include "geom/polyhedra.h"
#include "periodically.h"
#include "status-bar.h"
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
                      "why integer not null default 0, "
                      "why_face_idx integer not null default 0, "
                      "why_edge_idx integer not null default 0, "
                      "method integer not null, "
                      "createdate integer not null, "
                      "netness_numer integer not null, "
                      "netness_denom integer not null, "
                      "faces integer not null, "
                      "edges integer not null, "
                      "vertices integer not null, "
                      "example_net string not null, "
                      "invalid boolean not null default 0"
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

    const int why = r->GetInt(2);
    const int edge_idx = r->GetInt(3);
    const int face_idx = r->GetInt(4);
    switch (why) {
    case DB::WHY_ANY:
      hard.why = DB::Any{};
      break;
    case DB::WHY_LEAF_IH:
      hard.why = DB::LeafIH{
        .edge_idx = edge_idx,
        .face_idx = face_idx,
      };
      break;
    default:
      LOG(FATAL) << "Bad why value in database: " << why;
    }

    hard.method = r->GetInt(5);
    hard.createdate = r->GetInt(6);
    hard.netness_numer = r->GetInt(7);
    hard.netness_denom = r->GetInt(8);
    // This will produce nullopt for the empty string, which is how we
    // represent no net.
    hard.example_net = BitString::FromASCII(r->GetStringOpt(9).value_or(""));
    ret.push_back(std::move(hard));
  }
  return ret;
}

static constexpr std::string_view HARD_FIELDS =
  "id, poly, "
  "why, why_edge_idx, why_face_idx, "
  "method, createdate, "
  "netness_numer, netness_denom, example_net";

Hard DB::GetHard(int id) {
  std::vector<Hard> sols = GetHardForQuery(
    db->ExecuteString(
        std::format(
            "select {} "
            "from hard "
            "where id = {}", HARD_FIELDS, id)));
  CHECK(sols.size() == 1) << "Hard " << id << " not found";
  return sols[0];
}

void DB::AddHard(const Polyhedron &poly,
                 const Why &why,
                 int method,
                 int64_t netness_numer, int64_t netness_denom,
                 std::optional<BitString> example_net) {
  std::string polystring = StringFromVec3s(poly.vertices);
  std::string netstring;
  if (example_net.has_value()) netstring = example_net.value().ToASCII();
  const int faces = poly.faces->NumFaces();
  const int edges = poly.faces->NumEdges();
  const int vertices = poly.faces->NumVertices();

  // Flattened in database.
  int why_type = -1;
  int why_edge_idx = 0;
  int why_face_idx = 0;
  if (const Any *any = std::get_if<Any>(&why)) {
    (void)any;
    why_type = WHY_ANY;
  } else if (const LeafIH *leaf_ih = std::get_if<LeafIH>(&why)) {
    why_type = WHY_LEAF_IH;
    why_edge_idx = leaf_ih->edge_idx;
    why_face_idx = leaf_ih->face_idx;
  } else {
    LOG(FATAL) << "bad variant?";
  }

  db->ExecuteAndPrint(
      std::format(
          "insert into hard "
          "(poly, faces, edges, vertices, "
          "why, why_edge_idx, why_face_idx, "
          "method, createdate, netness_numer, netness_denom, example_net) "
          "values ('{}', "
          // faces, edges, vertices
          "{}, {}, {}, "
          // why
          "{}, {}, {}, "
          // method, time
          "{}, {}, "
          // netness, net
          "{}, {}, '{}')",
          polystring,
          faces, edges, vertices,
          why_type, why_edge_idx, why_face_idx,
          method,
          time(nullptr),
          netness_numer, netness_denom,
          netstring));
}

std::vector<Hard> DB::AllHard(bool include_invalid) {
  std::string query =
    std::format("select {} "
                "from hard", HARD_FIELDS);
  if (!include_invalid) {
    query += " where invalid = 0";
  }
  return GetHardForQuery(db->ExecuteString(query));
}

void DB::MarkValidity(int id, bool valid) {
  db->ExecuteAndPrint(
      std::format(
          "update hard "
          "set invalid = {} "
          "where id = {}",
          valid ? 0 : 1,
          id));
}


void DB::Spreadsheet(std::string_view filename) {
  std::string content = "id\tfaces\tedges\tverts"
    "\tmethod\tcreatedate\tnumer\tdenom\n";
  StatusBar status(1);
  Periodically status_per(1);
  std::vector<Hard> hards =
    GetHardForQuery(
        db->ExecuteString(
            std::format(
                "select {} "
                "from hard "
                "where invalid = 0 "
                "and why = {}",
                HARD_FIELDS,
                // TODO: Dump others?
                WHY_ANY)));
  for (int i = 0; i < (int)hards.size(); i++) {
    const Hard &h = hards[i];

    int nfaces = 0, nedges = 0, nverts = 0;
    if (std::optional<Polyhedron> opoly =
        PolyhedronFromConvexVertices(h.poly_points)) {
      nfaces = opoly.value().faces->NumFaces();
      nedges = opoly.value().faces->NumEdges();
      nverts = opoly.value().faces->NumVertices();
    }

    AppendFormat(&content, "{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n",
                 h.id, nfaces, nedges, nverts,
                 MethodName(h.method),
                 Util::FormatTime("%Y-%m-%d %H:%M:%S", h.createdate),
                 h.netness_numer, h.netness_denom);
    status_per.RunIf([&]{
        status.Progress(i + 1, hards.size(), "Exporting");
      });
  }
  status.Remove();

  Util::WriteFile(filename, content);
  Print("Wrote {}\n", filename);
}

void DB::DeleteHard(int id) {
  db->ExecuteAndPrint(std::format("delete from hard where id = {}", id));
}

void DB::UpdateHard(int id, int64_t netness_numer, int64_t netness_denom,
                    std::optional<BitString> example_net) {
  std::string netstring;
  if (example_net.has_value()) netstring = example_net.value().ToASCII();
  db->ExecuteAndPrint(
      std::format(
          "update hard "
          "set netness_numer = {}, netness_denom = {}, example_net = '{}' "
          "where id = {}",
          netness_numer, netness_denom, netstring, id));
}

void DB::Fixup() {
  std::vector<std::pair<int, std::string>> to_fix;
  {
    std::unique_ptr<Database::Query> q = db->ExecuteString(
        "select id, poly from hard "
        "where invalid = 0 and "
        "(faces = 0 or edges = 0 or vertices = 0 or "
        "faces is null or edges is null or vertices is null)");
    while (std::unique_ptr<Database::Row> r = q->NextRow()) {
      to_fix.emplace_back(r->GetInt(0), r->GetString(1));
    }
  }

  Periodically status_per(1, false);
  StatusBar status(1);
  int64_t processed = 0, fixed = 0;
  for (const auto &[id, poly_str] : to_fix) {
    std::optional<std::vector<vec3>> pts = Vec3sFromString(poly_str);
    if (!pts.has_value()) continue;

    if (std::optional<Polyhedron> opoly =
            PolyhedronFromConvexVertices(std::move(pts.value()))) {
      const int nfaces = opoly.value().faces->NumFaces();
      const int nedges = opoly.value().faces->NumEdges();
      const int nverts = opoly.value().faces->NumVertices();

      db->ExecuteAndPrint(
          std::format("update hard "
                      "set faces = {}, edges = {}, vertices = {} "
                      "where id = {}",
                      nfaces, nedges, nverts, id));
      fixed++;
    }
    processed++;
    status_per.RunIf([&]{
        status.Progress(processed, to_fix.size(), "Fixed {}", fixed);
      });
  }
  status.Remove();
}
