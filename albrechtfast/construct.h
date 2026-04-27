
#ifndef _ALBRECHT_CONSTRUCT_H
#define _ALBRECHT_CONSTRUCT_H

#include "geom/polyhedra.h"
#include "albrecht.h"
#include "yocto-math.h"
#include "arcfour.h"
#include "randutil.h"

// Constructs the polyhedron by building them in tandem with unfoldings that are
// crowded and hard to extend.

// Sketch of idea:
//
// We will have a 3D mesh (part of a convex polyhedron). It has an
// open boundary where it is incomplete. The representation should
// maintain this open boundary as a set of edges, so that it is
// easy to extend.
//
// We also have a set of valid 2D unfoldings of the mesh. This set
// is not complete (there are exponentially many). The unfoldings
// are non-overlapping.
//
// This is straightforward to initialize with a single face and
// the single unfolding of that.
//
// We also have state like "num_faces_left" (so that we don't
// continue forever).
//
// Then, proceed incrementally:
// If num_faces_left is zero, go into a mode where we close the
// mesh into a convex polyhedron, and return the polyhedron.
//
// Otherwise, we'll add a face. The face must extend the boundary. We
// should start by computing some kind of feasibility limits for this
// new face, because when we attach it in 3D it must still be possible
// for the object to be completed to a convex polyhedron. (So for
// example if the open boundary were the pentagonal top of a
// dodecahedron, the five faces that connect to that boundary create a
// finite space in which any new point must lie, or else the shape
// would not be convex. We probably want to keep a set of half-spaces
// along with our partly assembled polyhedron.) We choose this face
// shape such that it's hard (or impossible) to connect to the edge in
// as many candidate 2D unfoldings as we can. We then connect the face
// in the 3D polyhedron, updating the boundary and volume constraints.
// If the 2D unfolding was invalidated (because placing the face
// caused overlap) then we generate replacement unfoldings to keep the
// candidate set the same size. Repeat.

#endif
