
#include "albrecht.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "base/logging.h"
#include "base/print.h"
#include "bit-string.h"
#include "db.h"
#include "geom/polyhedra.h"
#include "opt/opt-seq.h"
#include "periodically.h"
#include "solve-dual-leaf.h"
#include "solve-leaf.h"
#include "solve-line.h"
#include "solve-strong.h"
#include "solve-vertex.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "yocto-math.h"

DECLARE_COUNTERS(ctr_wrong_connectivity, ctr_evals, ctr_final_poly_bad);
DECLARE_COUNTERS(ctr_wrong_size, ctr_wrong_degenerate, ctr_wrong_coplanar, ctr_wrong_convex, ctr_wrong_halfspace);

using Aug = Albrecht::AugmentedPoly;

// Takes a hard instance (for which we have no solutions)
// and improves it by making the faces less skinny (while
// preserving the unsolvability).

// Helper function to check that applying deltas to vertices preserves the
// exact same strict convex connectivity as the original polyhedron.
static bool CheckSameConnectivity(const Polyhedron &orig,
                                  const std::vector<vec3> &new_vertices) {
  if (new_vertices.size() != orig.vertices.size()) {
    ctr_wrong_size++;
    ctr_wrong_connectivity++;
    return false;
  }

  for (const std::vector<int> &face : orig.faces->v) {
    if (face.size() < 3) return false;
    vec3 v0 = new_vertices[face[0]];
    vec3 normal = {0, 0, 0};
    vec3 best_edge = {0, 0, 0};
    for (size_t i = 1; i < face.size(); i++) {
      vec3 edge = new_vertices[face[i]] - v0;
      if (yocto::length_squared(edge) > yocto::length_squared(best_edge)) {
        best_edge = edge;
      }
      if (i + 1 < face.size()) {
        vec3 n = yocto::cross(edge, new_vertices[face[i+1]] - v0);
        if (yocto::length_squared(n) > yocto::length_squared(normal)) {
          normal = n;
        }
      }
    }
    double len = yocto::length(normal);
    if (len < 1e-9) {
      ctr_wrong_degenerate++;
      ctr_wrong_connectivity++;
      return false;
    }
    normal /= len;

    bool above = false, below = false;
    std::vector<int> coplanar;

    for (int o = 0; o < (int)new_vertices.size(); o++) {
      double dot = yocto::dot(new_vertices[o] - v0, normal);
      if (dot < -0.00001) {
        if (above) {
          ctr_wrong_halfspace++;
          ctr_wrong_connectivity++;
          return false;
        }
        below = true;
      } else if (dot > 0.00001) {
        if (below) {
          ctr_wrong_halfspace++;
          ctr_wrong_connectivity++;
          return false;
        }
        above = true;
      } else {
        coplanar.push_back(o);
      }
    }

    if (!above && !below) {
      ctr_wrong_degenerate++;
      ctr_wrong_connectivity++;
      return false;
    }

    std::sort(coplanar.begin(), coplanar.end());
    std::vector<int> expected_face = face;
    std::sort(expected_face.begin(), expected_face.end());
    if (coplanar != expected_face) {
      ctr_wrong_coplanar++;
      ctr_wrong_connectivity++;
      return false;
    }

    // Check 2D convexity of the face.
    const vec3 ey = yocto::normalize(best_edge);
    const vec3 ex = yocto::cross(normal, ey);
    std::vector<vec2> poly2d;
    poly2d.reserve(face.size());
    for (int idx : face) {
      vec3 p = new_vertices[idx] - v0;
      poly2d.push_back(vec2{yocto::dot(p, ex), yocto::dot(p, ey)});
    }
    if (!IsPolyConvex(poly2d)) {
      ctr_wrong_convex++;
      ctr_wrong_connectivity++;
      return false;
    }
  }

  return true;
}

// Evaluate how "skinny" the faces of the polyhedron are.
// Returns a smaller loss when faces have improved their shapes.
static double EvaluateShapeLoss(const Albrecht::AugmentedPoly &aug) {
  double loss = 0.0;
  for (const std::vector<vec2> &poly2d : aug.polygons) {
    double area = std::abs(SignedAreaOfConvexPoly(poly2d));
    double perimeter = 0.0;
    for (size_t i = 0; i < poly2d.size(); i++) {
      vec2 p0 = poly2d[i];
      vec2 p1 = poly2d[(i + 1) % poly2d.size()];
      perimeter += yocto::length(p1 - p0);
    }
    if (area > 1e-9) {
      loss += (perimeter * perimeter) / area;
    } else {
      loss += 1e9;
    }
  }
  return loss;
}

// Evaluate the hardness of the polyhedron under the
// given constraint. Should return a high loss if we find a solution
// easily, and a lower loss if it requires many iterations or if no
// solution is found.
// The search is parallelized to count the number of sampling iterations.
static double EvaluateHardnessLoss(const DB::Why &why,
                                   const Albrecht::AugmentedPoly &aug,
                                   double shape_loss,
                                   double input_shape_loss) {
  if (shape_loss >= input_shape_loss) {
    // Skip expensive hardness evaluation if the shape hasn't improved.
    return 1e9;
  }

  static int seed = 0xCAFE;
  seed++;

  std::mutex m;
  int64_t iters = 0;
  bool solved = false;
  constexpr int MAX_ITERS = 100'000;
  constexpr int ITERS_PER_BATCH = 1024;

  constexpr int NUM_THREADS = 4;
  ParallelFan(NUM_THREADS, [&](int thread_idx) {
    ArcFour rc(std::format("eval.{}.{}", seed, thread_idx));
    for (;;) {
      {
        MutexLock ml(&m);
        if (solved) return;
        if (iters > MAX_ITERS) return;
      }

      for (int iter = 0; iter < ITERS_PER_BATCH; iter++) {
        BitString bs;
        if (auto *v = std::get_if<DB::VertexIH>(&why)) {
          bs = SolveVertex::SampleVertex(&rc, aug, v->vertex_idx);
        } else if (auto *l = std::get_if<DB::LeafIH>(&why)) {
          bs = SolveLeaf::SampleLeaf(&rc, aug, l->face_idx, l->edge_idx);
        } else {
          LOG(FATAL) << "Unhandled why-type: " << DB::WhyString(why);
        }

        if (Albrecht::IsNet(aug, bs)) {
          MutexLock ml(&m);
          iters += iter;
          solved = true;
          return;
        }
      }

      {
        MutexLock ml(&m);
        iters += ITERS_PER_BATCH;
      }
    }
  });

  ctr_evals++;

  if (solved) {
    return 1e9 / (1.0 + iters);
  }

  // No penalty if still unsolved.
  return 0.0;
}

// Number of samples that must fail to be solutions in order
// to consider an instance valid.
static constexpr int REQUIRE_SAMPLES = 1'000'000;

static void Improve(int id) {
  DB db;
  const DB::Hard hard = db.GetHard(id);

  Aug aug = [&]{
      CHECK(hard.netness_numer == 0 &&
            !hard.example_net.has_value()) << "But hard #" << id <<
        " does have a solution!\n";

      std::optional<Polyhedron> opoly =
        PolyhedronFromConvexVertices(hard.poly_points);
      CHECK(opoly.has_value()) << "Invalid polyhedron!";

      CHECK(IsWellConditioned(opoly.value().vertices));
      CHECK(IsManifold(opoly.value()));

      return Aug(std::move(opoly.value()));
    }();

  Print("Initial poly:\n");
  for (const vec3 &v : aug.poly.vertices) {
    Print("  {:.11g}, {:.11g}, {:.11g}\n",
          v.x, v.y, v.z);
  }

  double input_shape_loss = EvaluateShapeLoss(aug);
  Print("Initial shape loss: {:.4f}\n", input_shape_loss);

  CHECK(CheckSameConnectivity(aug.poly, aug.poly.vertices)) <<
    std::format("Initial polyhedron fails CheckSameConnectivity! "
                "Deg: {}, Cop: {}, Cvx: {}, Hsp: {}\n",
                ctr_wrong_degenerate.Read(), ctr_wrong_coplanar.Read(),
                ctr_wrong_convex.Read(), ctr_wrong_halfspace.Read());

  StatusBar status(3);
  Periodically status_per(1.0);

  double diam = Diameter(aug.poly);
  double delta = 0.05 * diam;
  std::vector<std::pair<double, double>> bounds;

  // Determine which faces are non-triangular. We fix the planes
  // of all non-triangular faces and only allow vertices to slide
  // along them, resolving "multiple constraints" by finding the
  // intersection of the planes.
  std::vector<bool> is_non_tri(aug.poly.faces->NumFaces(), false);
  for (int f = 0; f < aug.poly.faces->NumFaces(); ++f) {
    if (aug.poly.faces->v[f].size() > 3) {
      is_non_tri[f] = true;
    }
  }

  struct VertexParam {
    int v_idx;
    int num_constraints;
    vec3 base_pos;
    vec3 dir1, dir2;
  };
  std::vector<VertexParam> vparams;

  for (int i = 0; i < aug.poly.vertices.size(); ++i) {
    std::vector<int> incident_non_tri;
    for (int f = 0; f < aug.poly.faces->NumFaces(); ++f) {
      if (is_non_tri[f]) {
        for (int v : aug.poly.faces->v[f]) {
          if (v == i) {
            incident_non_tri.push_back(f);
            break;
          }
        }
      }
    }

    vec3 p = aug.poly.vertices[i];
    if (incident_non_tri.empty()) {
      vparams.push_back({i, 0, p, vec3{1,0,0}, vec3{0,1,0}});
      bounds.push_back({-delta, delta});
      bounds.push_back({-delta, delta});
      bounds.push_back({-delta, delta});
    } else if (incident_non_tri.size() == 1) {
      int f = incident_non_tri[0];
      vec3 v0 = aug.poly.vertices[aug.poly.faces->v[f][0]];
      vec3 normal = {0, 0, 0};
      vec3 e1 = {0, 0, 0};
      for (size_t k = 1; k < aug.poly.faces->v[f].size(); k++) {
        vec3 edge = aug.poly.vertices[aug.poly.faces->v[f][k]] - v0;
        if (yocto::length_squared(edge) > yocto::length_squared(e1)) {
          e1 = edge;
        }
        if (k + 1 < aug.poly.faces->v[f].size()) {
          vec3 n = yocto::cross(edge, aug.poly.vertices[aug.poly.faces->v[f][k+1]] - v0);
          if (yocto::length_squared(n) > yocto::length_squared(normal)) {
            normal = n;
          }
        }
      }
      vec3 n = yocto::normalize(normal);
      e1 = yocto::normalize(e1);
      vec3 e2 = yocto::cross(n, e1);
      vparams.push_back({i, 1, p, e1, e2});
      bounds.push_back({-delta, delta});
      bounds.push_back({-delta, delta});
    } else if (incident_non_tri.size() == 2) {
      auto normal_fn = [&](int f) {
        vec3 v0 = aug.poly.vertices[aug.poly.faces->v[f][0]];
        vec3 normal = {0, 0, 0};
        for (size_t k = 1; k + 1 < aug.poly.faces->v[f].size(); k++) {
          vec3 n = yocto::cross(aug.poly.vertices[aug.poly.faces->v[f][k]] - v0,
                                aug.poly.vertices[aug.poly.faces->v[f][k+1]] - v0);
          if (yocto::length_squared(n) > yocto::length_squared(normal)) {
            normal = n;
          }
        }
        return yocto::normalize(normal);
      };
      vec3 n1 = normal_fn(incident_non_tri[0]);
      vec3 n2 = normal_fn(incident_non_tri[1]);
      vec3 dir = yocto::cross(n1, n2);
      if (yocto::length_squared(dir) > 1e-12) {
        dir = yocto::normalize(dir);
        vparams.push_back({i, 2, p, dir, vec3{0,0,0}});
        bounds.push_back({-delta, delta});
      } else {
        vparams.push_back({i, 3, p, vec3{0,0,0}, vec3{0,0,0}});
      }
    } else {
      // 3 or more non-triangular faces fix the vertex completely.
      vparams.push_back({i, 3, p, vec3{0,0,0}, vec3{0,0,0}});
    }
  }

  status.Print("Optimizing {} parameters across {} vertices.\n",
               bounds.size(), vparams.size());

  OptSeq seq(bounds);
  double best_shape_loss = input_shape_loss;

  Periodically flush_per(5 * 60);
  std::optional<Polyhedron> recent_best;

  auto Flush = [&]{
      if (recent_best.has_value()) {
        int new_id =
          db.AddHard(recent_best.value(), hard.why, DB::METHOD_IMPROVE,
                     0, REQUIRE_SAMPLES, std::nullopt);
        status.Print("Wrote #" ACYAN("{}") " to database.\n", new_id);
        recent_best.reset();
      }
    };

  // Time limit of one hour.
  int64_t attempts = 0;
  Timer timer;
  while (timer.Seconds() < 3600) {
    status_per.RunIf([&]{
        std::string save_in = AGREY("nothing new");
        if (recent_best.has_value()) {
          save_in = std::format("save in {}", ANSI::Time(flush_per.SecondsLeft()));
        }

        status.Status("{} attempts in {}, {} evaluated, {} bad\n"
                      "{} wrong ({} deg, {} cop, {} cvx, {} hsp)\n"
                      "best shape {} -> {}. {}",
                      attempts,
                      ANSI::Time(timer.Seconds()),
                      ctr_evals.Read(),
                      ctr_final_poly_bad.Read(),
                      ctr_wrong_connectivity.Read(),
                      ctr_wrong_degenerate.Read(),
                      ctr_wrong_coplanar.Read(),
                      ctr_wrong_convex.Read(),
                      ctr_wrong_halfspace.Read(),
                      input_shape_loss,
                      best_shape_loss,
                      save_in);
      });

    std::vector<double> arg = seq.Next();
    attempts++;

    // Produce the new vertex locations using the parameters.
    std::vector<vec3> pts = aug.poly.vertices;
    int arg_idx = 0;
    for (const auto& vp : vparams) {
      if (vp.num_constraints == 0) {
        pts[vp.v_idx] = vp.base_pos +
            vec3{arg[arg_idx], arg[arg_idx+1], arg[arg_idx+2]};
        arg_idx += 3;
      } else if (vp.num_constraints == 1) {
        pts[vp.v_idx] = vp.base_pos +
            vp.dir1 * arg[arg_idx] + vp.dir2 * arg[arg_idx+1];
        arg_idx += 2;
      } else if (vp.num_constraints == 2) {
        pts[vp.v_idx] = vp.base_pos + vp.dir1 * arg[arg_idx];
        arg_idx += 1;
      }
    }

    if (!IsWellConditioned(pts) || !CheckSameConnectivity(aug.poly, pts)) {
      seq.Result(1e9);
      continue;
    }

    Polyhedron new_poly;
    new_poly.vertices = std::move(pts);
    new_poly.faces = aug.poly.faces;
    new_poly.name = "improve";

    Aug new_aug(std::move(new_poly));
    double shape_loss = EvaluateShapeLoss(new_aug);

    double hard_loss = EvaluateHardnessLoss(hard.why, new_aug,
                                            shape_loss, input_shape_loss);

    double loss = shape_loss + hard_loss;
    seq.Result(loss);

    // Only save to DB if it's still hard, and is a meaningful shape improvement.
    if (hard_loss == 0.0 && shape_loss < best_shape_loss - 1e-4) {
      // HERE: Check the validity of the polyhedron before recording it.
      const std::optional<Polyhedron> test_poly =
        PolyhedronFromConvexVertices(new_aug.poly.vertices);
      if (test_poly.has_value()) {
        best_shape_loss = shape_loss;
        status.Print("Improved shape loss to {:.4f}\n", shape_loss);
        recent_best = {std::move(new_aug.poly)};
      } else {
        ctr_final_poly_bad++;
      }
    }

    if (flush_per.ShouldRun()) {
      Flush();
    }
  }

  // Flush any final improvement that wasn't saved yet.
  Flush();
}



int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 2) << "./improve.exe database-id\n";

  int id = atoi(argv[1]);
  CHECK(id > 0);

  Improve(id);

  return 0;
}
