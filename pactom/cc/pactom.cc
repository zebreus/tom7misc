
#include "pactom.h"

#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <map>
#include <utility>

#include "xml.h"
#include "geom/latlon.h"
#include "util.h"
#include "base/logging.h"
#include "optional-iterator.h"
#include "re2/re2.h"

using namespace std;

PacTom::PacTom() {}

static vector<pair<LatLon, double>> ParseCoords(const string &error_context,
                                                const string &contents) {
  vector<string> coords = Util::Tokens(contents,
                                       [](char c) {
                                         return c == ' ' ||
                                           c == '\t' ||
                                           c == '\n' ||
                                           c == '\r';
                                       });

  vector<pair<LatLon, double>> out;
  out.reserve(coords.size());
  // Note that coordinates are given as lon,lat but standard
  // in LatLon is (lat, lon).
  for (const string &coord : coords) {
    vector<string> lle = Util::Fields(coord,
                                      [](char c) {
                                        return c == ',';
                                      });
    CHECK_EQ(lle.size(), 3) << error_context
                            << ": Expected lon,lat,elev but got "
                            << coord;
    auto RequireDouble = [&error_context, &coord](const string &f) {
        optional<double> od = Util::ParseDoubleOpt(f);
        CHECK(od.has_value()) << error_context
                              << ": Expected numeric lon,lat,elev: "
                              << coord;
        return od.value();
      };
    out.emplace_back(LatLon::FromDegs(RequireDouble(lle[1]),
                                      RequireDouble(lle[0])),
                     RequireDouble(lle[2]));
  }
  return out;
}

// Find the first descendant with the given tag name (case-sensitive),
// or return nullptr;
// XXX to xml.h?
static XML::Node *FindTag(XML::Node &node, string_view name) {
  if (node.type == XML::NodeType::Element) {
    if (node.tag == name)
      return &node;

    for (XML::Node &child : node.children)
      if (XML::Node *n = FindTag(child, name))
        return n;
  }
  return nullptr;
}

static std::optional<string> GetLeaf(XML::Node &node, string_view name) {
  if (XML::Node *c = FindTag(node, name)) {
    if (c->type == XML::NodeType::Element &&
        c->children.size() == 0) {
      return {""};
    } else if (c->type == XML::NodeType::Element &&
               c->children.size() == 1 &&
               c->children[0].type == XML::NodeType::Text) {
      return {c->children[0].contents};
    }
    return nullopt;
  }
  return nullopt;
}

// Require a descendant with <name>text</name> and return text.
static string RequireLeaf(XML::Node &node, string_view name) {
  optional<string> so = GetLeaf(node, name);
  CHECK(so.has_value()) << "Expected descendant <" << name << "> with text";
  return so.value();
}

static int MonthNum(const string &m) {
  if (m == "Jan") return 1;
  if (m == "Feb") return 2;
  if (m == "Mar") return 3;
  if (m == "Apr") return 4;
  if (m == "May") return 5;
  if (m == "Jun") return 6;
  if (m == "Jul") return 7;
  if (m == "Aug") return 8;
  if (m == "Sep") return 9;
  if (m == "Oct") return 10;
  if (m == "Nov") return 11;
  if (m == "Dec") return 12;
  CHECK(false) << "Bad month: " << m;
}

/*
before
Loaded 262 paths with 439778 waypoints.
There are 93 hoods
*/
struct KmlRec {
  using NodeType = XML::NodeType;
  using Node = XML::Node;

  KmlRec(const string &filename) : filename(filename) {}

  const string filename;
  // Populated by Process.
  vector<PacTom::Run> runs;

  // Due to the many different approaches I used to store these, there are multiple
  // different representations.
  //
  // <folder>
  //   <name>Actual Name</name>
  //   <description>Actual Desc</description>
  //   <placemark>
  //     <name>Track</name>
  //     <linestring>
  //       <coordinates>Actual Lat/Lon</coordinates>
  //       ...
  //
  // or older
  //
  // <placemark>
  //   <name>Actual Name</name>
  //   <description>Actual Desc</description>
  //   <linestring>
  //      <coordinates>Actual Lat/Lon</coordinates>
  //      ...
  //
  // So the strategy here is to recursively look for <linestring>, but
  // to keep track of the best name/desc from either the surrounding
  // folder of placemark tag.
  void Process(Node &node, string name_ctx, string desc_ctx) {
    if (node.type == NodeType::Element) {

      if (Util::lcase(node.tag) == "placemark" ||
          Util::lcase(node.tag) == "folder") {
        auto nameo = GetLeaf(node, "name");
        auto desco = GetLeaf(node, "description");
        if (nameo.has_value() && Util::lcase(nameo.value()) != "track") {
          name_ctx = nameo.value();
        }

        if (desco.has_value() && desco.value() != "") {
          desc_ctx = desco.value();
        }

        for (Node &child : node.children) {
          Process(child, name_ctx, desc_ctx);
        }
      } else if (Util::lcase(node.tag) == "linestring") {
        // This is presumed to be a run.
        PacTom::Run run;
        CHECK(name_ctx != "") << "linestring with no name";
        run.name = name_ctx;
        // TODO: Parse date from desc or name.
        int month = 0, day = 0, year = 0;
        string days, months;
#define ANY_RE "(?:a|[^a])*"
        if (RE2::FullMatch(desc_ctx,
                           ANY_RE ">([0-9]?[0-9])/([0-9]?[0-9])/([0-9][0-9])\\s"
                           ANY_RE,
                           &month, &day, &year)) {
          run.month = month;
          run.day = day;
          run.year = 2000 + year;
        } else if (RE2::FullMatch(
                       desc_ctx,
                       ANY_RE
                       "(...),\\s(...)\\s([0-9]?[0-9]),\\s([0-9][0-9][0-9][0-9])\\s"
                       ANY_RE,
                       &days, &months, &day, &year)) {
          run.month = MonthNum(months);
          run.day = day;
          run.year = year;
        } else {
          printf("Name [%s]\nDesc: [%s]\n",
                 name_ctx.c_str(), desc_ctx.c_str());
        }

        string coords = RequireLeaf(node, "coordinates");
        run.path = ParseCoords(filename, coords);
        runs.emplace_back(std::move(run));
      } else {
        for (Node &child : node.children) {
          Process(child, name_ctx, desc_ctx);
        }
      }
    }
  }
};


struct HoodRec {
  std::map<string, std::vector<LatLon>> polys;

  void Process(XML::Node &node) {
    if (node.type == XML::NodeType::Element) {
      if (node.tag == "Placemark") {

        string name = RequireLeaf(node, "name");
        string coords = RequireLeaf(node, "coordinates");

        CHECK(polys.find(name) == polys.end()) << "Duplicate "
          "neighborhoods: " << name;

        vector<pair<LatLon, double>> lle = ParseCoords(name, coords);

        // Strip elevation, which is 0.
        std::vector<LatLon> poly;
        poly.reserve(lle.size());
        for (const auto &[ll, elev_] : lle) poly.push_back(ll);

        polys[name] = std::move(poly);

      } else {
        for (XML::Node &child : node.children) {
          Process(child);
        }
      }

    }
  }
};

std::unique_ptr<PacTom> PacTom::FromFiles(const vector<string> &files,
                                          const optional<string> &hoodfile) {
  std::unique_ptr<PacTom> pactom(new PacTom);

  for (const string &file : files) {
    const string contents = Util::ReadFile(file);
    if (contents.empty()) return nullptr;

    string error;
    optional<XML::Node> nodeopt = XML::Parse(contents, &error);
    if (!nodeopt.has_value())
      return nullptr;

    XML::Node &node = nodeopt.value();
    KmlRec kmlrec(file);
    kmlrec.Process(node, "", "");
    for (auto &run : kmlrec.runs)
      pactom->runs.emplace_back(std::move(run));
    kmlrec.runs.clear();
  }

  for (const string &file : GetOpt(hoodfile)) {
    const string contents = Util::ReadFile(file);
    if (!contents.empty()) {
      string error;
      optional<XML::Node> nodeopt = XML::Parse(contents, &error);
      if (!nodeopt.has_value())
        return nullptr;

      XML::Node &node = nodeopt.value();
      HoodRec hoodrec;
      hoodrec.Process(node);

      pactom->hoods = std::move(hoodrec.polys);
    }
  }

  for (const auto &[name, poly] : pactom->hoods) {
    pactom->neighborhood_names.push_back(name);

    Bounds bounds;
    for (const LatLon pos : poly) {
      const auto [y, x] = pos.ToDegs();
      bounds.Bound(x, y);
    }

    pactom->hood_boxes.push_back(make_pair(bounds, &poly));
  }

  return pactom;
}

static bool PointInside(const std::vector<LatLon> &poly,
                        LatLon pos) {
  const auto [y, x] = pos.ToDegs();
  bool odd = false;

  int jdx = poly.size() - 1;
  for (int idx = 0; idx < poly.size(); idx++) {
    const auto [yi, xi] = poly[idx].ToDegs();
    const auto [yj, xj] = poly[jdx].ToDegs();


    if (yi > y != yj > y &&
        x < ((xj - xi) * (y - yi) / (yj - yi) + xi))
      odd = !odd;
    jdx = idx;
  }
  return odd;
}

int PacTom::InNeighborhood(LatLon pos) const {
  // Naive! PERF: Use a spatial data structure.

  const auto [y, x] = pos.ToDegs();
  for (int i = 0; i < hood_boxes.size(); i++) {
    const auto &[bbox, ppoly] = hood_boxes[i];
    if (bbox.Contains(x, y) && PointInside(*ppoly, pos)) {
      return i;
    }
  }

  return -1;
}
