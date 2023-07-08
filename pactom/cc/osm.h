
#ifndef _PACTOM_OSM_H
#define _PACTOM_OSM_H

#include "geom/latlon.h"

struct OSM {
  // Empty.
  OSM();

  enum Highway {
    // If not a highway.
    NONE,

    RESIDENTIAL,
    PRIMARY,
    SECONDARY,
    TERTIARY,
    SERVICE,
    STEPS,
    FOOT,
    MOTORWAY,
    MOTORWAYLINK,
    // This does not mean unknown!
    UNCLASSIFIED,
    OTHER,
  };

  struct Way {
    // Can be blank.
    std::string name;
    std::vector<uint64_t> points;
    Highway highway;
  };

  std::unordered_map<uint64_t, LatLon> nodes;
  std::unordered_map<uint64_t, Way> ways;

  void AddFile(const std::string &filename);

private:

};

#endif
