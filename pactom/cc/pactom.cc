
#include "pactom.h"

#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <map>
#include <utility>
#include <cmath>

#include "xml.h"
#include "geom/latlon.h"
#include "geom/latlon-tree.h"
#include "util.h"
#include "base/logging.h"
#include "optional-iterator.h"
#include "re2/re2.h"
#include "heap.h"

#include "threadutil.h"

using namespace std;

static constexpr double METERS_TO_FEET = 3.28084;

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
    if (lle.size() != 2 && lle.size() != 3) {
      CHECK(false) << error_context
                   << ": Expected lon,lat,elev? but got "
                   << coord;
    }
    auto RequireDouble = [&error_context, &coord](const string &f) {
        optional<double> od = Util::ParseDoubleOpt(f);
        CHECK(od.has_value()) << error_context
                              << ": Expected numeric lon,lat,elev: "
                              << coord;
        return od.value();
      };
    out.emplace_back(LatLon::FromDegs(RequireDouble(lle[1]),
                                      RequireDouble(lle[0])),
                     lle.size() > 2 ? RequireDouble(lle[2]) : 0.0);
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

template<class F>
static XML::Node *FindTagMatching(XML::Node &node, string_view name, F f) {
  if (node.type == XML::NodeType::Element) {
    if (node.tag == name)
      return &node;

    for (XML::Node &child : node.children)
      if (XML::Node *n = FindTag(child, name))
        return n;
  }
  return nullptr;
}

// Get the string contents of the first leaf node with the given name,
// whose contents passes the predicate.
template<class F>
static std::optional<string> GetLeafMatching(
    XML::Node &node, string_view name, const F &f) {

  if (node.type == XML::NodeType::Element) {
    if (node.tag == name) {
      if (node.children.size() == 0 && f(""))
        return {""};

      if (node.children.size() == 1 &&
          node.children[0].type == XML::NodeType::Text &&
          f(node.children[0].contents)) {
        return {node.children[0].contents};
      }
    }

    for (XML::Node &child : node.children) {
      auto ro = GetLeafMatching(child, name, f);
      if (ro.has_value()) return ro;
    }
  }

  return nullopt;
}

static void GetAll(XML::Node &node, string_view name,
                   std::vector<string> *out) {
  if (node.type == XML::NodeType::Element) {
    if (node.tag == name) {
      if (node.children.size() == 0)
        out->push_back("");

      if (node.children.size() == 1 &&
          node.children[0].type == XML::NodeType::Text) {
        out->push_back(node.children[0].contents);
      }
    }

    for (XML::Node &child : node.children) {
      GetAll(child, name, out);
    }
  }
}


// Require a descendant with <name>text</name> and return text.
static string RequireLeaf(XML::Node &node, string_view name) {
  optional<string> so = GetLeaf(node, name);
  CHECK(so.has_value()) << "Expected descendant <" << name << "> with text";
  return so.value();
}

#define MONTH3 "(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)"
static int MonthNum(const string &m, const string &err_ctx) {
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
  CHECK(false) << "Bad month: " << m << " in " << err_ctx;
  return 0;
}

// Return Y/M/D.
static std::optional<std::tuple<int, int, int>> ParseDesc(
    const string &name,
    const string &desc) {
  // TODO: Parse date from desc or name.
  int month = 0, day = 0, year = 0;
  string days, months;
#define ANY_RE "(?:a|[^a])*"
  if (RE2::FullMatch(desc,
                     ANY_RE ">([0-9]?[0-9])/([0-9]?[0-9])/([0-9][0-9])\\s"
                     ANY_RE,
                     &month, &day, &year)) {
    return {make_tuple(2000 + year, month, day)};
  } else if (RE2::FullMatch(
                 desc,
                 ANY_RE
                 "(...),\\s(" MONTH3 ")\\s([0-9]?[0-9]),\\s([0-9][0-9][0-9][0-9])\\s"
                 ANY_RE,
                 &days, &months, &day, &year)) {
    return {make_tuple(year, MonthNum(months, desc), day)};

  } else if (RE2::FullMatch(
                 desc,
                 ANY_RE
                 "(...),\\s([0-9]?[0-9])\\s(" MONTH3 ")\\s([0-9][0-9][0-9][0-9])\\s"
                 ANY_RE,
                 &days, &day, &months, &year)) {
    return {make_tuple(year, MonthNum(months, desc), day)};
  } else if (RE2::FullMatch(
                 desc,
                 // >Sat Sep 01 21:01:35 GMT 2018 by
                 ANY_RE
                 ">(...)\\s(" MONTH3 ")\\s([0-9][0-9])\\s[0-9:]+\\sGMT\\s([0-9][0-9][0-9][0-9])\\sby"
                 ANY_RE,
                 &days, &months, &day, &year)) {
    return {make_tuple(year, MonthNum(months, desc), day)};
  }

  return nullopt;
}

/*

Loaded 269 runs with 442338 waypoints.
There are 93 hoods

*/
struct KmlRec {
  using NodeType = XML::NodeType;
  using Node = XML::Node;

  KmlRec(const string &filename) : filename(filename) {}

  const string filename;
  string file_desc;

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
  void Process(Node &node, bool one_run_per_file) {
    if (one_run_per_file) {
      std::vector<string> all;
      GetAll(node, "description", &all);
      // Don't really care how this looks; we're just going to mine it
      // for dates.
      file_desc = Util::Join(all, "\n");
    }

    ProcessRec(node, "", "");

    if (one_run_per_file) {
      CHECK(runs.size() == 1) << filename << ": Expected one run per file; "
        "got " << runs.size();
    }
  }

  void ProcessRec(Node &node, string name_ctx, string desc_ctx) {
    if (node.type == NodeType::Element) {

      if (Util::lcase(node.tag) == "placemark" ||
          Util::lcase(node.tag) == "folder") {
        auto nameo = GetLeaf(node, "name");
        // Need to skip this description which prevents us from finding the
        // date within the "laps" folder for some activities, since there's
        // a description of the laps folder itself.
        auto desco = GetLeafMatching(
            node, "description",
            [](const string &d) {
              return d != "" &&
                d != "Laps, start and end splits recorded in the activity.";
            });

        if (nameo.has_value() && Util::lcase(nameo.value()) != "track") {
          name_ctx = nameo.value();
        }

        if (desco.has_value() && desco.value() != "") {
          desc_ctx = desco.value();
        }

        for (Node &child : node.children) {
          ProcessRec(child, name_ctx, desc_ctx);
        }
      } else if (Util::lcase(node.tag) == "linestring") {
        // This is presumed to be a run.
        PacTom::Run run;
        CHECK(name_ctx != "") << filename << ": linestring with no name";
        run.name = name_ctx;

        if (auto ymdo = ParseDesc(name_ctx, desc_ctx)) {
          std::tie(run.year, run.month, run.day) = ymdo.value();
        } else if (auto fymdo = ParseDesc(filename, file_desc)) {
          std::tie(run.year, run.month, run.day) = fymdo.value();
        } else {
          printf("Couldn't parse. Name: [%s|%s]\nDesc: [%s|%s]\n",
                 filename.c_str(), name_ctx.c_str(),
                 file_desc.c_str(), desc_ctx.c_str());
        }

        string coords = RequireLeaf(node, "coordinates");
        run.path = ParseCoords(filename, coords);
        runs.emplace_back(std::move(run));
      } else {
        for (Node &child : node.children) {
          ProcessRec(child, name_ctx, desc_ctx);
        }
      }
    }
  }
};


// This parses my "neighborhoods" file and also the KML
// county file from the Allegheny GIS site.
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

std::unique_ptr<PacTom> PacTom::FromFiles(
    const vector<string> &files,
    const optional<string> &hoodfile,
    const std::optional<std::string> &countyfile,
    bool one_run_per_file) {
  std::unique_ptr<PacTom> pactom(new PacTom);

  for (const string &file : files) {
    const string contents = Util::ReadFile(file);
    if (contents.empty()) {
      printf("%s: Empty or unreadable\n", file.c_str());
      return nullptr;
    }

    string error;
    optional<XML::Node> nodeopt = XML::Parse(contents, &error);
    if (!nodeopt.has_value()) {
      printf("%s: XML doesn't parse (%s)\n", file.c_str(), error.c_str());
      continue;
    }

    XML::Node &node = nodeopt.value();
    KmlRec kmlrec(file);
    kmlrec.Process(node, one_run_per_file);
    for (auto &run : kmlrec.runs)
      pactom->runs.emplace_back(std::move(run));
    kmlrec.runs.clear();
  }

  for (const string &file : GetOpt(hoodfile)) {
    if (!PacTom::LoadHoods(file,
                           &pactom->hoods,
                           &pactom->neighborhood_names,
                           &pactom->hood_boxes)) {
      return nullptr;
    }
  }

  for (const string &file : GetOpt(countyfile)) {
    if (!PacTom::LoadHoods(file,
                           &pactom->munis,
                           &pactom->muni_names,
                           &pactom->muni_boxes)) {
      return nullptr;
    }
  }

  return pactom;
}

bool PacTom::LoadHoods(
    const string &file,
    std::map<std::string, std::vector<LatLon>> *borders,
    std::vector<std::string> *names,
    std::vector<std::pair<Bounds, const std::vector<LatLon> *>> *boxes) {

  const string contents = Util::ReadFile(file);
  if (!contents.empty()) {
    string error;
    optional<XML::Node> nodeopt = XML::Parse(contents, &error);
    if (!nodeopt.has_value()) {
      printf("%s doesn't parse (%s)\n", file.c_str(), error.c_str());
      return false;
    }

    XML::Node &node = nodeopt.value();
    HoodRec hoodrec;
    hoodrec.Process(node);

    *borders = std::move(hoodrec.polys);
  }

  for (const auto &[name, poly] : *borders) {
    names->push_back(name);

    Bounds bounds;
    for (const LatLon pos : poly) {
      const auto [y, x] = pos.ToDegs();
      bounds.Bound(x, y);
    }

    boxes->push_back(make_pair(bounds, &poly));
  }
  return true;
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

int PacTom::InMuni(LatLon pos) const {
  // Naive! PERF: Use a spatial data structure.

  const auto [y, x] = pos.ToDegs();
  for (int i = 0; i < muni_boxes.size(); i++) {
    const auto &[bbox, ppoly] = muni_boxes[i];
    if (bbox.Contains(x, y) && PointInside(*ppoly, pos)) {
      return i;
    }
  }

  return -1;
}


double PacTom::RunMiles(const Run &run, bool use_elevation) {
  double res = 0.0;
  for (int i = 0; i < run.path.size() - 1; i++) {
    const auto &[latlon0, elev0] = run.path[i];
    const auto &[latlon1, elev1] = run.path[i + 1];
    double dist1 = LatLon::DistFeet(latlon0, latlon1);
    if (use_elevation) {
      double dz = (elev1 - elev0) * METERS_TO_FEET;
      res += sqrt(dz * dz + dist1 * dist1);
    } else {
      res += dist1;
    }
  }
  return res / 5280.0;
}

struct GraphNode {
  LatLon pos;
  // Unique list of neighbors. Could consider a set
  // data structure, but we expect these to be small.
  std::vector<int> neighbors;

  GraphNode(LatLon pos, std::vector<int> neighbors) :
    pos(pos), neighbors(std::move(neighbors)) {}
  void AddEdge(int other) {
    for (int o : neighbors) {
      if (o == other) return;
    }
    neighbors.push_back(other);
  }
};

// Distances in meters for this part.
//
// If points on the same path are greater than 250m apart, assume a
// data problem and don't create that edge.
static constexpr double PATH_ERROR = 250.0;
// Distance within which we create an implicit edge between nearby
// points, even if I didn't physically travel between them.
static constexpr double MERGE_DISTANCE = 25.0;
// Distance within which we just snap points to be the same node.
static constexpr double SAME_DISTANCE = 1.0;

struct Graph {
  // with index into nodes.
  LatLonTree<int> latlontree;

  std::vector<GraphNode> nodes;
  int AddNode(LatLon pos) {
    std::vector<std::tuple<LatLon, int, double>> close =
      latlontree.Lookup(pos, SAME_DISTANCE);
    // It's possible for there to be multiple nodes within the
    // snap distance (that didn't themselves snap together).
    // Take the closest one.
    if (!close.empty()) {
      std::sort(close.begin(),
                close.end(),
                [](const std::tuple<LatLon, int, double> &a,
                   const std::tuple<LatLon, int, double> &b) {
                  return std::get<2>(a) < std::get<2>(b);
                });
      return std::get<1>(close[0]);
    } else {
      // Otherwise, a new node.
      const int next_node = nodes.size();
      nodes.emplace_back(pos, std::vector<int>{});
      latlontree.Insert(pos, next_node);
      return next_node;
    }
  }

  // Add edges in both directions.
  void AddEdge(int from, int to) {
    CHECK(from >= 0 && from < nodes.size() &&
          to >= 0 && to < nodes.size() &&
          from != to);

    nodes[from].AddEdge(to);
    nodes[to].AddEdge(from);
  }

  // Might reuse an existing node if there's one that's
  // already close enough.
  int AddNodeWithEdge(int node_from, LatLon pos_to) {
    CHECK(node_from >= 0 && node_from < nodes.size());
    // TODO PERF: Can also find if this node would land on an existing
    // edge (and add a new node there, and return it) or cross an
    // existing edge...

    const int node_to = AddNode(pos_to);
    if (node_from != node_to) {
      AddEdge(node_from, node_to);
    }
    return node_to;
  }

};

PacTom::SpanningTree PacTom::MakeSpanningTree(LatLon home_pos,
                                              int num_threads) {
  Graph graph;

  printf("Build graph from runs: ");
  for (const auto &run : runs) {
    if (run.path.empty()) continue;

    LatLon last_pos = run.path[0].first;
    int node = graph.AddNode(last_pos);
    for (const auto &[ll, elev_] : run.path) {
      double dist = LatLon::DistMeters(last_pos, ll);
      if (dist > PATH_ERROR) {
        // Probably a data problem (lost GPS, etc.)
        // so do not connect them.
        last_pos = ll;
        node = graph.AddNode(last_pos);
      } else {

        last_pos = ll;
        node = graph.AddNodeWithEdge(node, ll);
      }
    }
    printf(".");
  }
  printf("\n");

  {
    printf("Link nearby nodes.\n");
    // This doesn't add nodes nor modify the latlon tree, so
    // we can do it in parallel. But the edge updates have to
    // be mutually exclusive.
    std::mutex m;
    ParallelComp(
        graph.nodes.size(),
        [&m, &graph](int idx) {
          LatLon pos = graph.nodes[idx].pos;
          std::vector<std::tuple<LatLon, int, double>> close =
            graph.latlontree.Lookup(pos, MERGE_DISTANCE);
          for (const auto &[ll, oidx, dist] : close) {
            if (idx != oidx) {
              MutexLock ml(&m);
              graph.AddEdge(idx, oidx);
            }
          }
        },
        num_threads);
  }

  printf("Get shortest paths.\n");
  // Get node closest to home. We assume there's a node
  // within 100m (better would be to use a GetClosest query
  // in the latlon tree, but it is not implemented).
  const auto [home_node_idx, home_node_dist] = [&]() {
      std::vector<std::tuple<LatLon, int, double>> close =
        graph.latlontree.Lookup(home_pos, 100.0);
      CHECK(!close.empty()) << "No node within 100m of home!";
      // Take the closest one.
      std::sort(close.begin(),
                close.end(),
                [](const std::tuple<LatLon, int, double> &a,
                   const std::tuple<LatLon, int, double> &b) {
                  return std::get<2>(a) < std::get<2>(b);
                });
      return make_pair(std::get<1>(close[0]), std::get<2>(close[0]));
    }();

  // Calculate shortest paths using Dijkstra's algorithm.
  // Rather than use intrusive heap locations, we keep this
  // array of otherwise-empty values parallel to the graph's
  // nodes.
  std::vector<Heapable> heapvalues(graph.nodes.size());
  auto HeapIdx = [&heapvalues](Heapable *h) {
      // (Don't divide by sizeof!)
      return h - &heapvalues[0];
    };
  // PERF
  for (int i = 0; i < heapvalues.size(); i++) {
    CHECK(HeapIdx(&heapvalues[i]) == i) << i;
  }

  std::unordered_map<int, double> done;
  using NodeHeap = Heap<double, Heapable>;
  NodeHeap nodeheap;
  nodeheap.Insert(home_node_dist, &heapvalues[home_node_idx]);
  while (!nodeheap.Empty()) {
    NodeHeap::Cell cell = nodeheap.PopMinimum();
    // Insert each of its neighbors with the path distance.
    const double dist = cell.priority;
    const int src_idx = HeapIdx(cell.value);
    CHECK(done.find(src_idx) == done.end());
    done[src_idx] = dist;
    CHECK(src_idx >= 0 && src_idx < graph.nodes.size());
    for (const int dst_idx : graph.nodes[src_idx].neighbors) {
      if (done.find(dst_idx) == done.end()) {
        // Not already finished.
        const double new_dist = dist +
          LatLon::DistMeters(graph.nodes[src_idx].pos,
                             graph.nodes[dst_idx].pos);
        Heapable *value = &heapvalues[dst_idx];
        if (value->location < 0) {
          // Not already inserted.
          nodeheap.Insert(new_dist, value);
        } else {
          // Adjust priority in place if smaller.
          NodeHeap::Cell ocell = nodeheap.GetCell(&heapvalues[dst_idx]);
          if (new_dist < ocell.priority) {
            nodeheap.AdjustPriority(value, new_dist);
          }
        }
      }
    }
  }

  printf("Make spanning tree.\n");

  auto GetDist = [&](int idx) {
      CHECK(idx >= 0 && idx < graph.nodes.size());
      auto it = done.find(idx);
      // CHECK(it != done.end());
      if (it == done.end()) return 1.0 / 0.0;
      return it->second;
    };

  SpanningTree stree;
  stree.nodes.resize(graph.nodes.size());
  stree.root = home_node_idx;
  // Each node can just be evaluated in parallel now.
  ParallelComp(
      graph.nodes.size(),
      [&](int idx) {
        const GraphNode &gnode = graph.nodes[idx];
        SpanningTree::Node *snode = &stree.nodes[idx];
        snode->pos = gnode.pos;
        // Could return inf if we had unreachable nodes.
        snode->dist = GetDist(idx);

        if (idx == home_node_idx) {
          snode->parent = -1;
        } else {
          // Get best parent.
          int besti = -1;
          double best_dist = 1.0 / 0.0;
          for (int oidx : gnode.neighbors) {
            double new_dist =
              LatLon::DistMeters(gnode.pos,
                                 graph.nodes[oidx].pos) +
              GetDist(oidx);
            if (new_dist < best_dist) {
              besti = oidx;
              best_dist = new_dist;
            }
          }
          // Might still be -1 if no neighbors, or
          // all infinite.
          snode->parent = besti;
        }
      },
      num_threads);

  return stree;
}
