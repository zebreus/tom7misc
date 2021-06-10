
#include "pactom.h"

#include <memory>
#include <vector>
#include <string>

#include "xml.h"
#include "geom/latlon.h"
#include "util.h"
#include "base/logging.h"

using namespace std;

PacTom::PacTom() {}

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
          vector<string> coords = Util::Tokens(node.children[0].contents,
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
            CHECK_EQ(lle.size(), 3) << filename
                                    << ": Expected lon,lat,elev but got "
                                    << coord;
            auto RequireDouble = [this, &coord](const string &f) {
                optional<double> od = Util::ParseDoubleOpt(f);
                CHECK(od.has_value()) << filename
                                      << ": Expected numeric lon,lat,elev: "
                                      << coord;
                return od.value();
              };
            out.emplace_back(LatLon::FromDegs(RequireDouble(lle[1]),
                                              RequireDouble(lle[0])),
                             RequireDouble(lle[2]));
          }
          paths.emplace_back(std::move(out));

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

std::unique_ptr<PacTom> PacTom::FromFiles(const vector<string> &files) {
  std::unique_ptr<PacTom> pactom(new PacTom);

  for (const string &file : files) {
    const string &contents = Util::ReadFile(file);
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

  return pactom;
}
