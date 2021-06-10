
#ifndef _PACTOM_H
#define _PACTOM_H

#include <memory>
#include <string>
#include <vector>
#include <utility>

#include "geom/latlon.h"

struct PacTom {

  static std::unique_ptr<PacTom> FromFiles(
      const std::vector<std::string> &files);

  // Position and elevation. No naming or timing.
  std::vector<std::vector<std::pair<LatLon, double>>> paths;

private:
  PacTom();

};


#endif
