
#ifndef _ALBRECHT_CONSTRUCT_H
#define _ALBRECHT_CONSTRUCT_H

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "geom/polyhedra.h"
#include "albrecht.h"
#include "yocto-math.h"
#include "arcfour.h"
#include "randutil.h"


// Constructs the polyhedron by building them in tandem with
// unfoldings that are crowded and hard to extend.

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
// This is straightforward to initialize with two faces connected by
// an edge with dihedral angle less than 180° (this makes the volume
// nonzero and the boundary unambiguous) and the single unfolding of
// that. One of the faces can be placed at z=0 without loss of
// generality.
//
// We also have state like "num_faces_left" (so that we don't
// continue forever).
//
// Then, proceed incrementally:
// If num_faces_left is zero, go into a mode where we close the
// mesh into a convex polyhedron (one way would be to just compute
// the convex hull of the current point set), and return the polyhedron.
//
// Otherwise, we'll add a face. The face must extend the boundary. We
// should start by computing some kind of feasibility limits for this
// new face, because when we attach it in 3D it must still be possible
// for the object to be completed to a convex polyhedron.
//
// To be more precise about that: For example if the open boundary
// were the pentagonal top of a dodecahedron, the five faces that
// connect to that boundary create a finite space in which any new
// point must lie, or else the shape would not be convex. We need to
// keep a set of half-spaces along with our partly assembled
// polyhedron. The space might be infinite (because two of these
// spaces are parallel like in the cube, or diverging like on the
// bottom half of an octahedron). When we consider placing a new face,
// all its points must be within the intersection of half spaces, and
// the half-space added by the face must not remove any vertices
// already placed. One way to do this is by picking an angle for the
// plane intersecting that edge where the face will lie (within
// feasible bounds). This then gives us a convex polygon (the
// intersection with the volume) where we can place face points. By
// this construction we'll always have a partly-assembled convex shape
// that can be closed when we run out of budget.
//
// Anyway, after determining some bounds from the above, we select a
// 2D face shape for the incremental update. This shape should be
// chosen such that it's hard (or impossible) to connect to the edge
// in as many candidate 2D unfoldings as we can. Usually choosing
// triangle faces is best, because they limit the branching (total
// number of edges). But large polygonal faces are also helpful for
// clogging up the space. Sometimes choosing such a face can be good
// if we see that it will create local dead ends (e.g. concavities
// like in the truncated cube example) and thus not actually increase
// the branching factor. We should also be careful not to shrink
// the feasible volume to zero too quickly (or we'll get very simple
// polyhedra).
//
// We then connect the face in the 3D polyhedron in a valid way,
// updating the boundary and volume constraints. If the 2D unfolding
// was invalidated (because placing the face caused overlap) then we
// generate replacement unfoldings to keep the candidate set the same
// size. Repeat.

// A 3D half-space. Points inside the feasible volume satisfy
// dot(normal, p) <= d.
struct HalfSpace {
  vec3 normal = {0.0, 0.0, 0.0};
  double d = 0.0;
};

// Represents a vertex in the partially constructed 3D mesh.
struct MeshVertex {
  vec3 pos = {0.0, 0.0, 0.0};
};

// Represents an oriented edge in the 3D mesh.
struct MeshEdge {
  int v0 = -1;
  int v1 = -1;
  // The face that this edge belongs to (when viewing from the outside,
  // the face is to the left of the edge from v0 to v1). Must
  // be present, even on the boundary.
  int left_face = -1;
  // The face on the right. If this is -1, the edge is part of the
  // open boundary.
  int right_face = -1;
};

// Represents a face in the partially constructed 3D mesh.
struct MeshFace {
  // Indices into the main vertices array, in counter-clockwise order
  // when viewed from the exterior.
  std::vector<int> vertices;
  // Indices into the main edges array, parallel to the vertices array.
  // The ith edge connects vertices[i] to vertices[(i + 1) % size].
  std::vector<int> edges;
  // The plane containing this face, directed outward.
  HalfSpace plane;
};

// Represents the 2D position of a single face in a specific unfolding.
struct UnfoldedFace {
  // The 2D coordinates of the face's vertices, in the same order as
  // MeshFace::vertices.
  std::vector<vec2> vertices;
};

// Represents a single, valid (non-overlapping) 2D unfolding of the
// partially constructed polyhedron.
//
// Correspondence between 3D and 2D:
//  - faces[i] is the 3D face, and unfoldings[u].faces[i] is its 2D shape.
//  - faces[i].vertices[j] corresponds to the 2D point at
//    unfoldings[u].faces[i].vertices[j].
//  - The edge faces[i].edges[j] corresponds to the 2D segment from
//    vertices[j] to vertices[(j + 1) % size] in that same unfolded face.
struct Unfolding {
  // Parallel to the 3D mesh's faces array.
  std::vector<UnfoldedFace> faces;
};

// Manages the state of the partially constructed polyhedron and its pool
// of valid 2D unfoldings.
struct PartialPolyhedron {
  PartialPolyhedron(ArcFour *rc,
                    int num_faces = 20,
                    int target_unfoldings = 100) :
    rc(rc),
    num_faces_left(num_faces),
    target_unfoldings(target_unfoldings) {

    Initialize();
    ReplenishUnfoldings();
  }

  // Compute the lower and upper bound on the dihedral angle for a
  // new face attached at edge_idx (which must be on the boundary).
  std::pair<double, double> ComputeFeasibleAngles(int edge_idx) const;

  // Computes the feasible 2D region for adding a new face at the given
  // boundary edge, assuming the new face lies in the plane specified
  // by the input angle.
  // The plane must intersect the boundary edge. The returned region is
  // a convex polygon defined by the intersection of the new plane with
  // all existing half-spaces.
  std::vector<vec2> ComputeFeasibleRegion(int edge_idx,
                                          double dihedral_angle) const;

  // Check if adding the candidate face points to the specified boundary
  // edge maintains convexity and manifold properties.
  bool IsFeasible(int boundary_edge_idx,
                  const std::vector<vec3> &new_face_pts) const;

  // Measure the amount of overlap (in the current population of
  // candidate unfoldings) created by a new polygonal face attached to
  // the indicated edge, if each unfolding contained that edge and
  // a new face of this shape. We use this as a metric to choose
  // new faces that maximize the difficulty of unfolding.
  // Returns the fraction of the population that has any overlap.
  double MeasureOverlapFraction(
      int boundary_edge_idx,
      // The candidate 2D face shape. Te segment from
      // poly[0] to poly[1] attaches to the boundary edge.
      // To preserve CCW winding order across the shared edge,
      // poly[0] corresponds to the 3D edge's v1 vertex, and
      // poly[1] corresponds to its v0 vertex.
      const std::vector<vec2> &poly) const;

  // Add a new face to the partial polyhedron, attached at the specified
  // boundary edge. This updates the boundary, half-spaces, and the
  // pool of unfoldings. Invalidates unfoldings that overlap.
  void AddFace(int boundary_edge_idx, const std::vector<vec3> &new_face_pts);

  // Attempts to generate replacement unfoldings when some are invalidated
  // by a newly added face, aiming to reach target_unfoldings. Typically
  // do this after AddFace.
  void ReplenishUnfoldings();

  // If num_faces_left reaches zero, we can close the mesh into a complete
  // convex polyhedron (e.g. by computing the convex hull of the vertices).
  std::optional<Polyhedron> Close() const;

  // The bounding box containing all vertices.
  std::pair<vec3, vec3> AABB() const {
    return std::make_pair(min_vertex, max_vertex);
  }

  // -- Accessors --

  int NumFaces() const;
  int NumVertices() const;
  int NumEdges() const;

  const MeshFace &GetFace(int face_idx) const {
    return faces[face_idx];
  }
  const MeshVertex &GetVertex(int vertex_idx) const {
    return vertices[vertex_idx];
  }
  const MeshEdge &GetEdge(int edge_idx) const {
    return edges[edge_idx];
  }

  // Returns indices of all edges that have right_face == -1.
  std::vector<int> GetBoundaryEdges() const;

  const std::vector<Unfolding> &GetUnfoldings() const {
    return unfoldings;
  }

  // Format the data structure to a string, for debugging.
  std::string DebugString() const;

  // Checks structural validity of the object. Aborts if something
  // is wrong. (Not fast; this is for debugging and tests.)
  void CheckValidity() const;

 private:
  void CheckIndices() const;
  void CheckHalfSpaces() const;
  void CheckUnfoldings() const;
  void CheckBoundary() const;
  void CheckSelfIntersections() const;
  void CheckPlanarityAndConvexity() const;
  void CheckDihedralAngles() const;
  void CheckUnfoldingWindingOrder() const;

  void UpdateAABB(const vec3 &v);

  // Initialize the state with two random faces connected by a dihedral
  // edge.
  void Initialize();

  // Random number generator; not owned.
  ArcFour *rc = nullptr;

  // Number of faces left to add.
  int num_faces_left = 10;
  // The target size for the pool of candidate unfoldings.
  int target_unfoldings = 100;

  std::vector<MeshVertex> vertices;
  std::vector<MeshEdge> edges;
  std::vector<MeshFace> faces;

  // The most negative and most positive corners of the AABB
  // containing all the vertices.
  vec3 min_vertex = {}, max_vertex = {};

  // Feasibility half-spaces. Currently, this can just be the outward
  // normal planes of every face added so far.
  std::vector<HalfSpace> half_spaces;

  // Pool of candidate non-overlapping 2D unfoldings.
  std::vector<Unfolding> unfoldings;

  bool HasSeparatingAxis(const std::vector<vec2> &poly1,
                         const std::vector<vec2> &poly2) const;
  // Checks whether the 2D polygons in a candidate unfolding overlap.
  bool IsUnfoldingValid(const Unfolding &unfolding) const;
};

// Sample a new face from a feasible polygon attached to
// a given edge.
struct FaceChooser {
  static constexpr double MAX_DIAMETER_RATIO = 1.5;
  vec3 p0 = {}, p1 = {};
  // Local coordinate system for the face. The x axis
  // is the edge being extended, and y positive
  // extends into the feasible polygon.
  vec3 origin = {}, x_dir = {}, y_dir = {};
  double edge_len = 0.0;
  vec2 v_top = {};
  double max_dist = 0.0;

  FaceChooser(
      // The feasible region. This will be on the 2D
      // segment (0, 0)-(edge_len, 0) where edge_len
      // is the length of the 3D edge.
      const std::vector<vec2> &feasible_poly,
      // The edge being extended.
      const vec3 &p0, const vec3 &p1,
      // The normal of the existing face that's being extended.
      // (the "left face" in a partial polyhedron). The dihedral
      // angle is measured from this.
      const vec3 &normal_left,
      // The dihedral angle.
      double angle,
      // The diameter of the current polyhedron,
      // which we use to limit the sampled face's size.
      double diameter) : p0(p0), p1(p1) {
    vec3 edge_dir = yocto::normalize(p1 - p0);
    vec3 outward_dir = yocto::cross(edge_dir, normal_left);

    // Calculate the normal of the plane containing the new face.
    vec3 n_new =
      std::cos(angle) * normal_left +
      std::sin(angle) * outward_dir;

    // Define a 2D local coordinate system for the new face.
    origin = p1;
    x_dir = yocto::normalize(p0 - p1);
    y_dir = yocto::cross(n_new, x_dir);

    edge_len = yocto::length(p0 - p1);

    v_top = {0.0, 0.0};
    for (int j = 0; j < (int)feasible_poly.size(); j++) {
      if (feasible_poly[j].y > v_top.y) {
        v_top = feasible_poly[j];
      }
    }
    CHECK(v_top.y > 1e-5) << "Polygon must have area in +y";

    max_dist = std::max(edge_len, diameter * MAX_DIAMETER_RATIO);
  }

  // Generates a 2D triangular face given two continuous parameters in [0, 1].
  // u determines the base point along the boundary edge.
  // v determines the height of the new vertex towards the feasible top.
  std::vector<vec2> Generate2DFace(double u, double v) const {
    static constexpr double LENGTH_MARGIN = 0.1;
    static constexpr double OM2L = 1.0 - 2.0 * LENGTH_MARGIN;

    double base_frac = LENGTH_MARGIN + OM2L * u;
    double t = LENGTH_MARGIN + OM2L * v;

    vec2 base_pt = {edge_len * base_frac, 0.0};
    vec2 p2_2d = base_pt + (v_top - base_pt) * t;

    double dist = yocto::length(p2_2d - base_pt);
    if (dist > max_dist) {
      p2_2d = base_pt + (p2_2d - base_pt) * (max_dist / dist);
    }

    return {{0.0, 0.0}, {edge_len, 0.0}, p2_2d};
  }

  // Converts a 2D face polygon into 3D using the chosen plane.
  std::vector<vec3> ConvertTo3D(std::span<const vec2> poly) const {
    std::vector<vec3> poly3d;
    poly3d.reserve(poly.size());
    for (int i = 0; i < (int)poly.size(); i++) {
      poly3d.push_back(origin + x_dir * poly[i].x + y_dir * poly[i].y);
    }
    return poly3d;
  }

};


#endif
