
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

struct KmlRec {
  using NodeType = XML::NodeType;
  using Node = XML::Node;

  KmlRec(const string &filename) : filename(filename) {}

  const string filename;
  // Populated by Process.
  vector<vector<pair<LatLon, double>>> paths;

  void Process(Node &node) {
    if (node.type == NodeType::Element) {
      if (node.tag == "coordinates") {
        if (node.children.size() == 1 &&
            node.children[0].type == NodeType::Text) {

          paths.emplace_back(ParseCoords(filename,
                                         node.children[0].contents));

        } else {
          LOG(FATAL) << filename << ": Expected <coordinates> node to have "
            "a single text child.";
        }
      } else if (node.tag == "GroundOverlay") {
        // Unimplemented, but see pactom.sml.
      } else {
        for (Node &child : node.children) {
          Process(child);
        }
      }
    }
  }
};

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

// Require a descendant with <name>text</name> and return text.
static string RequireLeaf(XML::Node &node, string_view name) {
  if (XML::Node *c = FindTag(node, name)) {
    if (c->type == XML::NodeType::Element &&
        c->children.size() == 0) {
      return "";
    } else if (c->type == XML::NodeType::Element &&
               c->children.size() == 1 &&
               c->children[0].type == XML::NodeType::Text) {
      return c->children[0].contents;
    }
    CHECK(false) << "Expected <" << name << "> to just contain text.";
  }
  CHECK(false) << "Expected descandant <" << name << ">";
}

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
    kmlrec.Process(node);
    for (auto &path : kmlrec.paths)
      pactom->paths.emplace_back(std::move(path));
    kmlrec.paths.clear();
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
