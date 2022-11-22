
#include "osm.h"

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <utility>

#include "xml.h"
#include "geom/latlon.h"
#include "util.h"
#include "base/logging.h"

using namespace std;
using NodeType = XML::NodeType;
using Node = XML::Node;

OSM::OSM() {}

static pair<string, string> GetTag(const Node &node) {
  CHECK(node.type == NodeType::Element);
  string k, v;
  auto kit = node.attrs.find("k");
  if (kit != node.attrs.end()) k = kit->second;
  auto vit = node.attrs.find("v");
  if (vit != node.attrs.end()) v = vit->second;
  return make_pair(k, v);
}

static OSM::Highway ParseHighway(const string &value) {
  string t = Util::lcase(value);

  if (t == "") return OSM::NONE;
  if (t == "residential") return OSM::RESIDENTIAL;
  if (t == "primary") return OSM::PRIMARY;
  if (t == "secondary") return OSM::SECONDARY;
  if (t == "tertiary") return OSM::TERTIARY;
  if (t == "service") return OSM::SERVICE;
  if (t == "steps") return OSM::STEPS;
  if (t == "foot") return OSM::FOOT;
  if (t == "motorway") return OSM::MOTORWAY;
  if (t == "motorwaylink") return OSM::MOTORWAYLINK;
  if (t == "unclassified") return OSM::UNCLASSIFIED;

  return OSM::OTHER;
}

std::optional<uint64_t> GetAttr64(const Node &node,
                                  const string &attr) {
  auto it = node.attrs.find(attr);
  if (it != node.attrs.end()) {
    uint64_t r = atoll(it->second.c_str());
    return {r};
  }
  return nullopt;
}

void OSM::AddFile(const string &filename) {
  const string contents = Util::ReadFile(filename);
  CHECK(!contents.empty()) << filename;

  string error;
  optional<XML::Node> topopt = XML::Parse(contents, &error);
  CHECK(topopt.has_value()) << filename << ": " << error;

  printf("Loaded %lld bytes from %s\n", contents.size(), filename.c_str());
  XML::Node &top = topopt.value();
  CHECK(top.type == NodeType::Element &&
        top.tag == "osm") << filename << ": "
                          << "Expected top-level <osm> tag.\n";
  printf("<osm> children: %d\n", top.children.size());

  for (const Node &ch : top.children) {
    if (ch.type == NodeType::Element) {
      if (ch.tag == "node") {
        if (auto ido = GetAttr64(ch, "id")) {
          // printf("<node id %llu>\n", ido.value());
          auto latit = ch.attrs.find("lat");
          auto lonit = ch.attrs.find("lon");
          if (latit != ch.attrs.end() &&
              lonit != ch.attrs.end()) {
            optional<double> lat = Util::ParseDoubleOpt(latit->second);
            optional<double> lon = Util::ParseDoubleOpt(lonit->second);
            CHECK(lat.has_value() && lon.has_value()) <<
              "Bad lat/lon in node " << ido.value();
            if (nodes.find(ido.value()) == nodes.end()) {
              // Duplicates are okay between tiles.
              nodes[ido.value()] = LatLon::FromDegs(lat.value(), lon.value());
            }
          }
        }
      } else if (ch.tag == "way") {
        if (auto ido = GetAttr64(ch, "id")) {
          Way way;
          for (const Node &wch : ch.children) {
            if (wch.type == NodeType::Element) {
              if (wch.tag == "nd") {
                if (auto ro = GetAttr64(wch, "ref")) {
                  way.points.push_back(ro.value());
                }
              } else {
                const auto [k, v] = GetTag(wch);
                if (k == "name") {
                  way.name = v;
                } else if (k == "highway") {
                  way.highway = ParseHighway(v);
                }
                // Other key/val pairs ignored.
              }
            }
          }
          // XXX Do we need to do anything to merge, like if a long
          // road spans multiple tiles?
          if (ways.find(ido.value()) == ways.end()) {
            ways[ido.value()] = std::move(way);
          }
        }
      }
    }
  }
}

