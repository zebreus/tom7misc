
#ifndef _BRECHTFAST_EXAMPLES_H
#define _BRECHTFAST_EXAMPLES_H

#include "albrecht.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "arcfour.h"
#include "bit-string.h"

struct Examples {
  std::vector<Albrecht::DebugResult> non_nets;
  std::vector<Albrecht::DebugResult> nets;
};

// This face (with any connecting edge) must be a leaf.
struct LeafFaceConstraint {
  int face_idx = 0;
};

// Leaf IH: This face must be a leaf, connected only on the edge.
struct LeafConstraint {
  int face_idx = 0;
  int edge_idx = 0;
};

// This cut edge (on this face) must be on the convex hull of
// the entire unfolding.
struct HullConstraint {
  int face_idx = 0;
  int edge_idx = 0;
};

// This edge is cut, and the attached faces on each side of it
// are leaves in the graph.
struct DualLeafConstraint {
  int edge_idx = 0;
};

// The unfolding must have a single path.
struct LineConstraint { };

// Anything.
struct NoConstraint { };

// Constraints on the shape of the unfolding.
using Constraint = std::variant<
  NoConstraint,
  LeafFaceConstraint,
  LeafConstraint,
  DualLeafConstraint,
  HullConstraint,
  LineConstraint>;

// Parse command-line arguments to find a constraint (or return
// NoConstraint), modifying the vector of arguments in place.
Constraint ParseConstraints(std::vector<std::string> *args);

// Generates up to the requested number of valid nets and non-nets for
// the given polyhedron. If face_idx is specified, the face must be a
// leaf, and if edge_idx is also specified, then that single edge on
// that face must be connected. Not guaranteed to find the requested
// number; e.g. for some polyhedra, all unfoldings are nets!
Examples GetSomeExamples(
    ArcFour *rc,
    const Albrecht::AugmentedPoly &aug,
    const Constraint constraint,
    const std::optional<BitString> &example_net,
    int num_nets, int num_non_nets, bool verbose);

#endif
