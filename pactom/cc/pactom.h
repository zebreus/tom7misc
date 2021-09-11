
#ifndef _PACTOM_H
#define _PACTOM_H

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <utility>
#include <map>

#include "geom/latlon.h"

struct PacTom {

  static std::unique_ptr<PacTom> FromFiles(
      const std::vector<std::string> &files,
      const std::optional<std::string> &neighborhoods);

  // Position and elevation. No naming or timing.
  std::vector<std::vector<std::pair<LatLon, double>>> paths;

  // Borders of neighborhoods, if loaded.
  std::map<std::string, std::vector<LatLon>> hoods;

 private:
  PacTom();

};


#endif
