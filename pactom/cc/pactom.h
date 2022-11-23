
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

 private:
  PacTom();

  std::vector<std::pair<Bounds, const std::vector<LatLon> *>> hood_boxes;
};


#endif
