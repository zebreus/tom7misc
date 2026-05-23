#include "sampler.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <optional>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "construct.h"
#include "geom/hull-2d.h"
#include "geom/point-map.h"
#include "geom/polyhedra.h"
#include "geom/symmetry-groups.h"
#include "netness.h"
#include "opt/opt.h"
#include "periodically.h"
#include "randutil.h"
#include "status-bar.h"
#include "timer.h"
#include "yocto-math.h"


DECLARE_COUNTERS(ctr_degenerate, ctr_too_big,
                 ctr_ill_conditioned, ctr_not_manifold, ctr_no_angle,
                 ctr_no_feasible, ctr_face_not_feasible);

using OneSample = Sampler::OneSample;

// Each of x,y,z in [-1, 1].
static vec3 RandomVec(ArcFour *rc) {
  return vec3(2.0 * RandDouble(rc) - 1.0,
              2.0 * RandDouble(rc) - 1.0,
              2.0 * RandDouble(rc) - 1.0);
}

std::string Sampler::SampleStats() {
  return std::format(
      "{} ill, {} notman, {} degen, {} noθ, {} no∆, {}✘, {} big",
      ctr_ill_conditioned.Read(),
      ctr_not_manifold.Read(),
      ctr_degenerate.Read(),
      ctr_no_angle.Read(),
      ctr_no_feasible.Read(),
      ctr_face_not_feasible.Read(),
      ctr_too_big.Read());
}

static std::optional<Polyhedron> CarefulPolyhedron(
    std::vector<vec3> verts) {
  if (!IsWellConditioned(verts)) {
    ctr_ill_conditioned++;
    return std::nullopt;
  }


  auto opoly = PolyhedronFromVertices(std::move(verts));
  if (!opoly.has_value())
    return std::nullopt;

  if (!IsManifold(opoly.value())) {
    ctr_not_manifold++;
    return std::nullopt;
  }

  return opoly;
}

static constexpr bool TRIANGLES_ONLY = false;

// Map the leaf constraint from the PP to the Polyhedron that results
// from closing it (which reorders edges etc.).
static std::optional<std::pair<int, int>> MapConstraint(
    const PartialPolyhedron &pp,
    const Polyhedron &poly) {
  if (!pp.GetLeafConstraint().has_value()) {
    return std::nullopt;
  }

  const auto [pp_fidx, pp_eidx] = pp.GetLeafConstraint().value();

  vec3 pp_centroid = {0.0, 0.0, 0.0};
  const auto &pp_face = pp.GetFace(pp_fidx);
  for (int v : pp_face.vertices) {
    pp_centroid += pp.GetVertex(v).pos;
  }
  pp_centroid /= pp_face.vertices.size();

  int best_fidx = -1;
  double best_f_dist = 1e18;
  for (int f = 0; f < poly.faces->NumFaces(); ++f) {
    vec3 op_centroid = {0.0, 0.0, 0.0};
    for (int v : poly.faces->v[f]) {
      op_centroid += poly.vertices[v];
    }
    op_centroid /= poly.faces->v[f].size();
    double d = distance_squared(pp_centroid, op_centroid);
    if (d < best_f_dist) {
      best_f_dist = d;
      best_fidx = f;
    }
  }

  const auto &pp_edge = pp.GetEdge(pp_eidx);
  vec3 p0 = pp.GetVertex(pp_edge.v0).pos;
  vec3 p1 = pp.GetVertex(pp_edge.v1).pos;

  int op_v0 = -1;
  double best_v0_dist = 1e18;
  int op_v1 = -1;
  double best_v1_dist = 1e18;

  for (int i = 0; i < (int)poly.vertices.size(); ++i) {
    double d0 = distance_squared(poly.vertices[i], p0);
    if (d0 < best_v0_dist) {
      best_v0_dist = d0;
      op_v0 = i;
    }
    double d1 = distance_squared(poly.vertices[i], p1);
    if (d1 < best_v1_dist) {
      best_v1_dist = d1;
      op_v1 = i;
    }
  }

  int best_eidx = -1;
  for (int e = 0; e < poly.faces->NumEdges(); ++e) {
    const auto &edge = poly.faces->edges[e];
    if ((edge.v0 == op_v0 && edge.v1 == op_v1) ||
        (edge.v0 == op_v1 && edge.v1 == op_v0)) {
      best_eidx = e;
      break;
    }
  }

  // If we weren't able to match them up, just fail to produce
  // a mapping.
  if (best_f_dist > 1e-4) return std::nullopt;
  if (best_v0_dist > 1e-4) return std::nullopt;
  if (best_v1_dist > 1e-4) return std::nullopt;
  if (best_fidx == -1 || best_eidx == -1) return std::nullopt;

  return std::make_pair(best_fidx, best_eidx);
}


static std::pair<Polyhedron, std::optional<std::pair<int, int>>>
MakeConstructInternal(StatusBar *status,
                      ArcFour *rc,
                      int max_faces,
                      bool leaf_ih) {
  for (;;) {
    const int target_faces = 12 + RandTo(rc, 29);
    PartialPolyhedron pp(rc, target_faces, 100);

    // At a random point, we introduce a leaf constraint.
    int leaf_target = [&]() {
        if (!leaf_ih) return max_faces + 1;

        int r = std::min(RandTo(rc, target_faces),
                         RandTo(rc, target_faces));
        return std::clamp(r, 4, target_faces - 2);
      }();

    while (pp.NumFaces() < target_faces) {
      if (leaf_ih && pp.NumFaces() >= leaf_target &&
          !pp.GetLeafConstraint().has_value()) {
        int best_count = 999999;
        std::vector<std::pair<int, int>> best_constraints;

        for (int f = 0; f < pp.NumFaces(); f++) {
          // To ensure f can be a leaf, check that removing f does not
          // disconnect the dual graph. While a completed convex
          // polyhedron's dual graph is 3-connected (so any face can
          // be a leaf), this PartialPolyhedron's open patch can have
          // cut vertices (e.g. if it's just a chain).
          int start_face = (f == 0) ? 1 : 0;
          std::vector<int> q = {start_face};
          std::vector<bool> vis(pp.NumFaces(), false);
          vis[f] = true;
          vis[start_face] = true;
          int reached = 1;
          int head = 0;
          while (head < (int)q.size()) {
            int curr = q[head++];
            for (int ce : pp.GetFace(curr).edges) {
              const MeshEdge &edge = pp.GetEdge(ce);
              int neighbor = (edge.left_face == curr) ? edge.right_face :
                                                        edge.left_face;
              if (neighbor != -1 && !vis[neighbor]) {
                vis[neighbor] = true;
                q.push_back(neighbor);
                reached++;
              }
            }
          }

          if (reached < pp.NumFaces() - 1) continue;

          for (int e : pp.GetFace(f).edges) {
            // The tree uses internal edges, so boundary edges are not
            // valid choices.
            if (pp.GetEdge(e).right_face == -1) continue;

            int count = 0;
            for (const Unfolding &unf : pp.GetUnfoldings()) {
              int connected_count = 0;
              bool target_connected = false;
              for (int fe : pp.GetFace(f).edges) {
                if (std::find(unf.tree_edges.begin(),
                              unf.tree_edges.end(), fe) !=
                    unf.tree_edges.end()) {
                  if (fe == e) target_connected = true;
                  connected_count++;
                }
              }
              if (target_connected && connected_count == 1) {
                count++;
              }
            }

            if (count < best_count) {
              best_count = count;
              best_constraints.clear();
              best_constraints.emplace_back(f, e);
            } else if (count == best_count) {
              best_constraints.emplace_back(f, e);
            }
          }
        }

        if (!best_constraints.empty()) {
          auto [best_f, best_e] =
              best_constraints[RandTo(rc, (int)best_constraints.size())];
          pp.SetLeafConstraint(best_f, best_e);
        } else {
          // Could not find a valid constraint; retry next iteration.
          leaf_target++;
        }
      }

      std::vector<int> b_edges = pp.GetBoundaryEdges();
      if (b_edges.empty()) {
        break;
      }

      if (pp.GetLeafConstraint().has_value()) {
        // To keep the constrained face as a leaf, we avoid adding new faces
        // directly to its boundary. It will be closed when the hull is built.
        int leaf_f = pp.GetLeafConstraint().value().first;
        std::vector<int> filtered;
        for (int e : b_edges) {
          if (pp.GetEdge(e).left_face != leaf_f) {
            filtered.push_back(e);
          }
        }
        if (!filtered.empty()) {
          b_edges = std::move(filtered);
        } else {
          // No edges left that can be expanded; fall back to the
          // convex hull closure.
          break;
        }
      }

      Shuffle(rc, &b_edges);

      auto [min_v, max_v] = pp.AABB();
      const double diameter = yocto::length(max_v - min_v);

      bool face_added = false;
      for (int edge_idx : b_edges) {
        auto [min_angle, max_angle] = pp.ComputeFeasibleAngles(edge_idx);
        if (max_angle < min_angle + 1e-3) {
          continue;
        }

        double subtended = max_angle - min_angle;
        if (subtended < 1.0e-5) {
          ctr_no_angle++;
          continue;
        }

        double angle = min_angle + RandDouble(rc) * subtended;
        std::vector<vec2> feasible_poly =
            pp.ComputeFeasibleRegion(edge_idx, angle);

        // Insist that we have a polygon with area and y
        // extent; this is a precondition of FaceChooser.
        {
          double max_y = 0.0;
          for (const vec2 &p : feasible_poly) {
            max_y = std::max(max_y, p.y);
          }

          if (max_y < 1.0e-5) {
            ctr_no_feasible++;
            continue;
          }
        }

        const MeshEdge &e = pp.GetEdge(edge_idx);
        vec3 p0 = pp.GetVertex(e.v0).pos;
        vec3 p1 = pp.GetVertex(e.v1).pos;
        vec3 normal_left = pp.GetFace(e.left_face).plane.normal;

        FaceChooser chooser(feasible_poly, p0, p1, normal_left,
                            angle, diameter);

        static constexpr int NUM_FACE_SAMPLES = 100;
        std::optional<std::vector<vec2>> best_poly;
        if constexpr (TRIANGLES_ONLY) {
          double best_overlap = -1.0;
          for (int sample = 0; sample < NUM_FACE_SAMPLES; ++sample) {
            double u = RandDouble(rc);
            double v = RandDouble(rc);
            std::vector<vec2> poly = chooser.Triangular2DFace(u, v);

            std::vector<vec3> face3d = chooser.ConvertTo3D(poly);
            if (pp.FeasibilityProblem(edge_idx, face3d)) continue;

            double overlap = pp.MeasureOverlapFraction(edge_idx, poly);
            if (overlap > best_overlap) {
              best_overlap = overlap;
              best_poly = {std::move(poly)};
            }
          }
        } else {

          double best_overlap = -1.0;
          for (int sample = 0; sample < NUM_FACE_SAMPLES; sample++) {
            int num_extra = 1;
            while (num_extra < 6 && rc->Byte() < 128) {
              num_extra++;
            }

            std::vector<vec2> pts;
            pts.reserve(num_extra + 2);
            pts.push_back({0.0, 0.0});
            pts.push_back({chooser.edge_len, 0.0});

            for (int i = 0; i < num_extra; i++) {
              vec2 pt = {0.0, 0.0};
              double sum = 0.0;
              for (const vec2 &fp : feasible_poly) {
                // -log(U) gives exponential distribution for Dirichlet weights.
                double w = -std::log(std::max(1.0e-9, RandDouble(rc)));
                pt.x += w * fp.x;
                pt.y += w * fp.y;
                sum += w;
              }
              pt.x /= sum;
              pt.y /= sum;

              vec2 base_pt = {chooser.edge_len * 0.5, 0.0};
              double dist = yocto::length(pt - base_pt);
              if (dist > chooser.max_dist) {
                pt = base_pt + (pt - base_pt) * (chooser.max_dist / dist);
              }
              pts.push_back(pt);
            }

            std::vector<int> poly_indices = Hull2D::GrahamScan(pts);
            if (poly_indices.size() < 3) continue;

            std::vector<vec2> poly;
            poly.reserve(poly_indices.size());
            for (int idx : poly_indices) poly.push_back(pts[idx]);

            // Ensure CCW winding so that the base edge is
            // {0,0} -> {edge_len,0}.
            double area = 0.0;
            for (int i = 0; i < (int)poly.size(); i++) {
              vec2 pa = poly[i];
              vec2 pb = poly[(i + 1) % poly.size()];
              area += pa.x * pb.y - pb.x * pa.y;
            }
            if (area < 0.0) {
              std::reverse(poly.begin(), poly.end());
            }

            // Reorient the polygon to guarantee the attaching boundary edge
            // is exactly the segment from {0,0} to {edge_len, 0}.
            int start_idx = -1;
            for (int i = 0; i < (int)poly.size(); i++) {
              vec2 p0 = poly[i];
              vec2 p1 = poly[(i + 1) % poly.size()];
              if (std::abs(p0.x) < 1.0e-5 && std::abs(p0.y) < 1.0e-5 &&
                  std::abs(p1.x - chooser.edge_len) < 1.0e-5 &&
                  std::abs(p1.y) < 1.0e-5) {
                start_idx = i;
                break;
              }
            }

            if (start_idx < 0) continue;

            if (start_idx != 0) {
              std::vector<vec2> reordered;
              reordered.reserve(poly.size());
              for (int i = 0; i < (int)poly.size(); i++) {
                reordered.push_back(poly[(start_idx + i) % poly.size()]);
              }
              poly = std::move(reordered);
            }

            // Clean up the polygon to ensure strictly convex vertices.
            // If an angle is too flat, remove a vertex. We never remove
            // the required base vertices at index 0 and 1.
            bool changed = true;
            while (changed && poly.size() > 3) {
              changed = false;
              for (int i = 0; i < (int)poly.size(); i++) {
                vec2 p_prev = poly[(i + poly.size() - 1) % poly.size()];
                vec2 p_curr = poly[i];
                vec2 p_next = poly[(i + 1) % poly.size()];

                vec2 e1 = p_curr - p_prev;
                vec2 e2 = p_next - p_curr;

                double len1 = yocto::length(e1);
                double len2 = yocto::length(e2);

                // Check for short edges or angles that are too flat
                // (sin(θ) <= 1e-3)
                if (len1 < 1e-3 || len2 < 1e-3 ||
                    (e1.x * e2.y - e1.y * e2.x) <= 1.0e-3 * len1 * len2) {
                  int remove_idx = i;
                  if (i == 0) remove_idx = (int)poly.size() - 1;
                  else if (i == 1) remove_idx = 2;

                  poly.erase(poly.begin() + remove_idx);
                  changed = true;
                  break;
                }
              }
            }

            // Final strict convexity check (also catches poly.size() == 3)
            bool strictly_convex = true;
            for (int i = 0; i < (int)poly.size(); i++) {
              vec2 p_prev = poly[(i + poly.size() - 1) % poly.size()];
              vec2 p_curr = poly[i];
              vec2 p_next = poly[(i + 1) % poly.size()];

              vec2 e1 = p_curr - p_prev;
              vec2 e2 = p_next - p_curr;

              double len1 = yocto::length(e1);
              double len2 = yocto::length(e2);

              if (len1 < 1e-3 || len2 < 1e-3 ||
                  (e1.x * e2.y - e1.y * e2.x) <= 1.0e-3 * len1 * len2) {
                strictly_convex = false;
                break;
              }
            }
            if (!strictly_convex) continue;

            std::vector<vec3> face3d = chooser.ConvertTo3D(poly);
            if (pp.FeasibilityProblem(edge_idx, face3d)) continue;

            double overlap = pp.MeasureOverlapFraction(edge_idx, poly);
            if (overlap > best_overlap) {
              best_overlap = overlap;
              best_poly = {std::move(poly)};
            }
          }


        }

        if (best_poly.has_value()) {
          std::vector<vec3> best_face = chooser.ConvertTo3D(best_poly.value());
          if (const char *problem =
              pp.FeasibilityProblem(edge_idx, best_face)) {
            status->Print("Not Feasible: " AGREY("{}") "\n", problem);
            ctr_face_not_feasible++;
            continue;
          }

          pp.AddFace(edge_idx, best_face);
          pp.ReplenishUnfoldings();
          face_added = true;
          break;
        }
      }

      if (!face_added) {
        break;
      }
    }

    std::optional<Polyhedron> opoly = pp.Close();
    if (!opoly.has_value()) {
      ctr_degenerate++;
      continue;
    }

    if (!IsManifold(opoly.value())) {
      ctr_not_manifold++;
      continue;
    }

    if (opoly.value().faces->NumFaces() >= max_faces) {
      ctr_too_big++;
      continue;
    }    std::optional<std::pair<int, int>> mapped_constraint =
        MapConstraint(pp, opoly.value());

    return std::make_pair(std::move(opoly.value()), mapped_constraint);
  }
}
Polyhedron Sampler::MakeConstruct(StatusBar *status,
                                  ArcFour *rc,
                                  int max_faces,
                                  bool leaf_ih) {
  return MakeConstructInternal(status, rc, max_faces, leaf_ih).first;
}

Sampler::LeafIHSample Sampler::ConstructHardLeaf(StatusBar *status,
                                                 ArcFour *rc,
                                                 int max_faces) {
  for (;;) {
    auto [poly, constraint] =
      MakeConstructInternal(status, rc, max_faces, true);
    if (constraint.has_value()) {
      const auto &[fidx, eidx] = constraint.value();
      return LeafIHSample{
        .poly = std::move(poly),
        .face_idx = fidx,
        .edge_idx = eidx,
      };
    }
  }
}

OneSample Sampler::ConstructSample(StatusBar *status,
                                   ArcFour *rc,
                                   int max_faces) {
  Timer sample_timer;
  Polyhedron poly = MakeConstruct(status, rc, max_faces, false);
  const double sample_sec = sample_timer.Seconds();

  Timer measure_timer;
  Aug aug(std::move(poly));
  auto [numer, denom] = Netness::Compute(Rand64(rc),
                                         aug,
                                         32768, 64, 1);

  double measure_sec = measure_timer.Seconds();

  return OneSample{
    .aug = std::move(aug),
    .numer = numer,
    .denom = denom,
    .sample_sec = sample_sec,
    .measure_sec = measure_sec,
  };
}


// from noperts. See discussion there.
Polyhedron Sampler::RandomSymmetricPolyhedron(ArcFour *rc, int num_points,
                                              int max_faces) {
  static const SymmetryGroups *symmetry = new SymmetryGroups;

  static constexpr SymmetryGroup GROUPS_ENABLED = SYM_TETRAHEDRAL |
    SYM_ICOSAHEDRAL | SYM_OCTAHEDRAL;

  auto MakePoints = [rc](int num) {
      std::vector<vec3> pts;
      pts.reserve(num);
      for (int i = 0; i < num; i++) {
        vec3 v = normalize(RandomVec(rc));
        pts.push_back(v);
      }
      return pts;
    };

  for (;;) {

    // Depending on the symmetry chosen, we'll need fewer
    // random points.
    int target_points = num_points;

    const bool include_reflection = rc->Byte() & 1;
    if (include_reflection)
      target_points = std::max(target_points / 2, 1);

    const char *method = "error";
    std::vector<vec3> points;
    // Select symmetry groups if we have enough points and if a biased
    // coin flip comes in our favor. There's no point in doing it
    // unless we'll generate at least two seed points, since otherwise
    // we just get a rotated version of that polyhedron.
    auto UsePolyhedralGroup = [&](
        const char *what,
        const SymmetryGroups::Group &group, int chance) -> bool {
        if (target_points >= group.points &&
            RandTo(rc, chance) == 0) {
          PointSet3 pointset;

          while (pointset.Size() < target_points) {
            std::vector<vec3> todo = {
              normalize(RandomVec(rc))
            };

            // Run until quiescence, even if we exceed the target
            // point size.
            while (!todo.empty()) {
              if (pointset.Size() > 1000) {

                DebugPointCloudAsSTL(pointset.Points(),
                                     "too-big.stl");

                LOG(FATAL) << "Something is wrong";
              }

              vec3 v = todo.back();
              todo.pop_back();

              if (!pointset.Contains(v)) {
                // identity is not included.
                pointset.Add(v);
              }

              for (const frame3 &rot : group.rots) {
                vec3 vr = yocto::transform_point(rot, v);
                if (pointset.Contains(vr)) {
                  // Skip.
                } else {
                  pointset.Add(vr);
                  todo.push_back(vr);
                }
              }
            }
          }

          points = pointset.Points();

          method = what;
          return true;
        }
        return false;
      };

    if ((GROUPS_ENABLED & SYM_ICOSAHEDRAL) &&
        UsePolyhedralGroup("icosahedron", symmetry->icosahedron, 3)) {
      // nothing
    } else if ((GROUPS_ENABLED & SYM_OCTAHEDRAL) &&
               UsePolyhedralGroup("octahedron", symmetry->octahedron, 3)) {
      // nothing
    } else if ((GROUPS_ENABLED & SYM_TETRAHEDRAL) &&
               UsePolyhedralGroup("tetrahedron", symmetry->tetrahedron, 3)) {
      // nothing
    } else {
      // Then we can always use the cyclic (or dihedral if reflections
      // are on) group.

      // TODO: Consider sometimes generating m>3 points on planes, to
      // create non-triangular facets.

      // Pick a nontrivial n, but not too big.
      int n = std::max((int)RandTo(rc, target_points), 2);
      target_points = std::max(target_points / n, 2);

      std::vector<vec3> r = MakePoints(target_points);
      for (int i = 0; i < n; ++i) {
        double angle = (2.0 * std::numbers::pi * i) / n;
        // Rotate around the z-axis.
        frame3 rotation_frame = yocto::rotation_frame(vec3{0.0, 0.0, 1.0},
                                                      angle);
        for (const vec3 &pt : r) {
          points.push_back(yocto::transform_point(rotation_frame, pt));
        }
      }
      method = "cyclic";
    }

    if (include_reflection) {
      std::vector<vec3> refl_pts = points;
      for (const vec3 &p : points) {
        refl_pts.emplace_back(p.x, -p.y, p.z);
      }
      points = std::move(refl_pts);
    }

    // Deduplicate points if they are too close. This is particularly
    // important when reflections are included.
    {
      std::vector<vec3> dedup_pts;
      dedup_pts.reserve(points.size());
      for (const vec3 &p : points) {
        for (const vec3 &q : dedup_pts) {
          if (distance_squared(p, q) < 0.0001) {
            goto next;
          }
        }
        dedup_pts.push_back(p);
      next:;
      }

      points = std::move(dedup_pts);
    }

    std::optional<Polyhedron> poly =
      PolyhedronFromConvexVertices(std::move(points), "randomsymmetric");
    if (poly.has_value()) {
      CHECK(!poly.value().vertices.empty());
      if (poly.value().faces->NumFaces() >= max_faces) {
        ctr_too_big++;
        continue;
      }
      return std::move(poly.value());
    } else {
      ctr_degenerate++;
    }
  }
}

OneSample Sampler::OptSample(StatusBar *status,
                             ArcFour *rc) {

  // Naive black-box optimization.

  const int num_verts = 12 + RandTo(rc, 20);

  // To ensure that the shape has volume, we require one
  // point to fall in each octant.

  // Vertices are flattened as x,y,z for the optimizer.
  std::vector<double> lbs(3 * num_verts), ubs(3 * num_verts);
  for (int i = 0; i < num_verts; i++) {
    for (int axis = 0; axis < 3; axis++) {
      if (i < 8) {
        if (i & (1 << axis)) {
          lbs[i * 3 + axis] = +0.5;
          ubs[i * 3 + axis] = +1.0;
        } else {
          lbs[i * 3 + axis] = -1.0;
          ubs[i * 3 + axis] = -0.5;
        }

      } else {
        lbs[i * 3 + axis] = -1.0;
        ubs[i * 3 + axis] = +1.0;
      }
    }
  }

  auto MakePoly = [num_verts](std::span<const double> pts) {
      CHECK(pts.size() == num_verts * 3);
      std::vector<vec3> vertices(num_verts);
      for (size_t i = 0; i < num_verts; i++) {
        vertices[i] = {pts[i * 3 + 0], pts[i * 3 + 1], pts[i * 3 + 2]};
      }

      return CarefulPolyhedron(std::move(vertices));
    };

  Periodically status_per(1);
  int64_t calls = 0;
  Timer timer;
  static constexpr double LARGE_LOSS = 1.0e10;
  auto Loss = [&](std::span<const double> pts) -> double {
      std::optional<Polyhedron> opoly = MakePoly(pts);
      if (!opoly.has_value()) {
        return LARGE_LOSS;
      }

      Aug aug(std::move(opoly.value()));

      // 128 samples per thread provides a modest 1024 total samples.
      auto [numer, denom] = Netness::Compute(Rand64(rc), aug, 128, 4, 1);

      calls++;
      if ((calls % 128) == 0) {
        status_per.RunIf([&]{
            status->LineStatus(
                SAMPLE_LINE,
                "{} calls in {}", calls, ANSI::Time(timer.Seconds()));
          });
      }

      return numer / static_cast<double>(denom);
    };

  // Just do one optimization pass so that we can get finer-grained
  // parallelism.
  const auto &[best, loss] =
    Opt::Minimize(num_verts * 3, Loss, lbs, ubs, 1000,
                  1, 1, Rand64(rc));

  std::optional<Polyhedron> opoly = MakePoly(best);
  CHECK(opoly.has_value());
  Aug aug(std::move(opoly.value()));
  auto [numer, denom] = Netness::Compute(Rand64(rc), aug, 131072, 8, 1);
  return OneSample{.aug = std::move(aug), .numer = numer, .denom = denom};
}


// All of the vertices on the unit sphere.
Polyhedron Sampler::RandomCyclicPolyhedron(ArcFour *rc, int num_points) {
  for (;;) {
    std::vector<vec3> pts;
    pts.reserve(num_points);
    for (int i = 0; i < num_points; i++) {
      vec3 v = normalize(RandomVec(rc));
      pts.push_back(v);
    }

    std::optional<Polyhedron> poly =
      PolyhedronFromConvexVertices(std::move(pts), "randomcyclic");
    if (poly.has_value()) {
      return std::move(poly.value());
    } else {
      ctr_degenerate++;
    }
  }
}
