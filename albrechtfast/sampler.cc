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
      "{} ill, {} notman, {} degen, {} noθ, {} no∆, {}✘",
      ctr_ill_conditioned.Read(),
      ctr_not_manifold.Read(),
      ctr_degenerate.Read(),
      ctr_no_angle.Read(),
      ctr_no_feasible.Read(),
      ctr_face_not_feasible.Read());
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

OneSample Sampler::ConstructSample(StatusBar *status,
                                   ArcFour *rc,
                                   int max_faces) {
  Timer sample_timer;
  for (;;) {
    const int target_faces = 12 + RandTo(rc, 29);
    PartialPolyhedron pp(rc, target_faces, 100);

    while (pp.NumFaces() < target_faces) {
      std::vector<int> b_edges = pp.GetBoundaryEdges();
      if (b_edges.empty()) {
        break;
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

            // Ensure CCW winding so that the base edge is {0,0} -> {edge_len,0}.
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
            break;
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
    }

    const double sample_sec = sample_timer.Seconds();

    Timer measure_timer;
    Aug aug(std::move(opoly.value()));
    auto [numer, denom] = Netness::Compute(Rand64(rc),
                                           aug,
                                           32768, 64, 1);

    double measure_sec = measure_timer.Seconds();

    OneSample sample{
      .aug = std::move(aug),
      .numer = numer,
      .denom = denom,
      .sample_sec = sample_sec,
      .measure_sec = measure_sec,
    };


    return sample;
  }
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
