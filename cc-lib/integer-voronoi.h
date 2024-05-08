
// Computes

#ifndef _CC_LIB_INTEGER_VORONOI_H
#define _CC_LIB_INTEGER_VORONOI_H

#include <vector>
#include <utility>

struct IntegerVoronoi {

  // Returns a vector of size width * height. Each pixel gives
  // the index of the input point that it is closest to.
  static std::vector<int>
  RasterizeVec(const std::vector<std::pair<int, int>> &points,
               int width, int height);

};


#endif
