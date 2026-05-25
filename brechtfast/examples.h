
#ifndef _BRECHTFAST_EXAMPLES_H
#define _BRECHTFAST_EXAMPLES_H

#include "albrecht.h"

#include <optional>
#include <vector>

#include "arcfour.h"
#include "bit-string.h"

struct Examples {
  std::vector<Albrecht::DebugResult> non_nets;
  std::vector<Albrecht::DebugResult> nets;
};

// Generates up to the requested number of valid nets and non-nets for
// the given polyhedron. If face_idx is specified, the face must be a
// leaf, and if edge_idx is also specified, then that single edge on
// that face must be connected. Not guaranteed to find the requested
// number; e.g. for some polyhedra, all unfoldings are nets!
Examples GetSomeExamples(
    ArcFour *rc,
    const Albrecht::AugmentedPoly &aug,
    std::optional<int> face_idx,
    std::optional<int> edge_idx,
    const std::optional<BitString> &example_net,
    int num_nets, int num_non_nets, bool verbose);

#endif
