
#ifndef _PACTOM_H
#define _PACTOM_H

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <utility>
#include <map>

#include "geom/latlon.h"
#include "bounds.h"

struct PacTom {

  static std::unique_ptr<PacTom> FromFiles(
      const std::vector<std::string> &files,
      const std::optional<std::string> &neighborhoods,
      const std::optional<std::string> &countyfile,
      // If true, will mine any description from the
      // file for the run's date.
      bool one_run_per_file = false);

  // Parallel to above.
  struct Run {
    std::string name;
    // e.g. 2000, 1, 1, for 1 January 2000.
    // Note: Might not have a date :/
    int year = 0, month = 0, day = 0;
    // Position and elevation, but no timing info.
    std::vector<std::pair<LatLon, double>> path;
  };
  std::vector<Run> runs;

  // The length of the run in miles. Considers the small gain in
  // surface distance from running up/down hills if use_elevation
  // is true.
  static double RunMiles(const Run &run, bool use_elevation = true);

  // Borders of neighborhoods, if loaded.
  std::map<std::string, std::vector<LatLon>> hoods;
  std::vector<std::string> neighborhood_names;
  // Return -1 if not in any neighborhood; otherwise neighborhood id.
  int InNeighborhood(LatLon pos) const;

  // Like above, but for municipalities in Allegheny County (if loaded).
  std::map<std::string, std::vector<LatLon>> munis;
  std::vector<std::string> muni_names;
  int InMuni(LatLon pos) const;

  struct SpanningTree {
    struct Node {
      LatLon pos;
      // Distance to root.
      double dist = 0.0;
      // Adjacent node that gets us closer to root, or -1
      // if this is the root (or if the node is not connected
      // to the root).
      int parent = 0;
    };
    std::vector<Node> nodes;
    int root = 0;
  };

  SpanningTree MakeSpanningTree(LatLon home_pos,
                                int num_threads = 8);

 private:
  PacTom();

  // Load neighborhoods or municipalities, which are represented the
  // same way. Returns false on failure.
  static bool LoadHoods(
      const std::string &file,
      std::map<std::string, std::vector<LatLon>> *borders,
      std::vector<std::string> *names,
      std::vector<std::pair<Bounds, const std::vector<LatLon> *>> *boxes);

  std::vector<std::pair<Bounds, const std::vector<LatLon> *>> hood_boxes;
  std::vector<std::pair<Bounds, const std::vector<LatLon> *>> muni_boxes;
};


#endif
