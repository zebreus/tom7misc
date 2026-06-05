
#ifndef _BRECHTFAST_FOLDING_H
#define _BRECHTFAST_FOLDING_H

#include <optional>
#include <vector>

#include "geom/polyhedra.h"
#include "yocto-math.h"

// Attempt to fold an unfolding back into a convex polyhedron.

struct Folding {

  struct UnfoldedMesh {
    // The location of vertices in the mesh.
    std::vector<vec2> vertices;

    // The polygons in the mesh. These become faces in the
    // folded polyhedron.
    std::vector<std::vector<int>> polygons;
  };

  // Returns a polyhedron with the same number of faces, if it
  // is successfully folded.
  static std::optional<Polyhedron> Fold(const UnfoldedMesh &umesh);

};

#endif
