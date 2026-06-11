
#include "albrecht.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
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
#include "util.h"
#include "yocto-math.h"

DECLARE_COUNTERS(ctr_rounds, ctr_wrong_connectivity, ctr_evals, ctr_final_poly_bad);
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

  // Add a planarity error term with a high coefficient to keep faces planar.
  loss += PlanarityError(aug.poly) * 1e6;

  for (const std::vector<vec2> &poly2d : aug.polygons) {
    double area = std::abs(SignedAreaOfConvexPoly(poly2d));
    double perimeter = 0.0;
    for (size_t i = 0; i < poly2d.size(); i++) {
      vec2 p_prev = poly2d[(i + poly2d.size() - 1) % poly2d.size()];
      vec2 p0 = poly2d[i];
      vec2 p1 = poly2d[(i + 1) % poly2d.size()];

      vec2 e1 = p0 - p_prev;
      vec2 e2 = p1 - p0;
      double len1 = yocto::length(e1);
      double len2 = yocto::length(e2);

      perimeter += len2;

      if (len1 > 1e-9 && len2 > 1e-9) {
        double cross = (e1.x * e2.y - e1.y * e2.x) / (len1 * len2);
        double abs_sin = std::abs(cross);
        // Cubic penalty as the angle approaches 0 or 180 degrees.
        loss += 1.0 / std::max(abs_sin * abs_sin * abs_sin, 1e-9);
      } else {
        loss += 1e9;
      }
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
                                   double input_shape_loss,
                                   int multiplier = 1) {
  ctr_evals++;

  if (auto *v = std::get_if<DB::VertexIH>(&why)) {
    if (aug.poly.faces->NumEdges() < 25) {
      std::optional<int64_t> difficulty =
        SolveVertex::Prove(aug, v->vertex_idx);
      // Impossible, as desired.
      if (!difficulty.has_value()) return 0;
      return 1'000'000'000 - difficulty.value();
    }
  }

  if (shape_loss >= input_shape_loss) {
    // Skip expensive hardness evaluation if the shape hasn't improved.
    return 1e9;
  }

  static int seed = 0xCAFE;
  seed++;

  std::mutex m;
  int64_t iters = 0;
  bool solved = false;
  const int64_t max_iters = 100'000LL * multiplier;
  constexpr int ITERS_PER_BATCH = 1024;

  constexpr int NUM_THREADS = 4;
  ParallelFan(NUM_THREADS, [&](int thread_idx) {
    ArcFour rc(std::format("eval.{}.{}", seed, thread_idx));
    for (;;) {
      {
        MutexLock ml(&m);
        if (solved) return;
        if (iters > max_iters) return;
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

  if (solved) {
    return 1e9 / (1.0 + iters);
  }

  // No penalty if still unsolved.
  return 0.0;
}

// Number of samples that must fail to be solutions in order
// to consider an instance valid.
static constexpr int REQUIRE_SAMPLES = 1'000'000;

class Parameterization {
 public:
  virtual ~Parameterization() = default;

  virtual void Prepare(const Polyhedron &poly, double delta) = 0;
  virtual int NumParameters() const = 0;
  virtual std::vector<std::pair<double, double>> Bounds() const = 0;
  virtual Polyhedron Apply(std::span<const double> params) const = 0;
};

class SlideNonTriangular : public Parameterization {
 private:
  struct VertexParam {
    int v_idx;
    int num_constraints;
    vec3 base_pos;
    vec3 dir1, dir2;
  };

  Polyhedron base_poly_;
  std::vector<VertexParam> vparams_;
  std::vector<std::pair<double, double>> bounds_;

 public:
  void Prepare(const Polyhedron &poly, double delta) override {
    base_poly_ = poly;
    bounds_.clear();
    vparams_.clear();

    // Determine which faces are non-triangular. We fix the planes
    // of all non-triangular faces and only allow vertices to slide
    // along them, resolving "multiple constraints" by finding the
    // intersection of the planes.
    std::vector<bool> is_non_tri(poly.faces->NumFaces(), false);
    for (int f = 0; f < poly.faces->NumFaces(); ++f) {
      if (poly.faces->v[f].size() > 3) {
        is_non_tri[f] = true;
      }
    }

    for (int i = 0; i < poly.vertices.size(); ++i) {
      std::vector<int> incident_non_tri;
      for (int f = 0; f < poly.faces->NumFaces(); ++f) {
        if (is_non_tri[f]) {
          for (int v : poly.faces->v[f]) {
            if (v == i) {
              incident_non_tri.push_back(f);
              break;
            }
          }
        }
      }

      vec3 p = poly.vertices[i];
      if (incident_non_tri.empty()) {
        vparams_.push_back({i, 0, p, vec3{1, 0, 0}, vec3{0, 1, 0}});
        bounds_.push_back({-delta, delta});
        bounds_.push_back({-delta, delta});
        bounds_.push_back({-delta, delta});

      } else if (incident_non_tri.size() == 1) {
        int f = incident_non_tri[0];
        vec3 v0 = poly.vertices[poly.faces->v[f][0]];
        vec3 normal = {0, 0, 0};
        vec3 e1 = {0, 0, 0};
        for (size_t k = 1; k < poly.faces->v[f].size(); k++) {
          vec3 edge = poly.vertices[poly.faces->v[f][k]] - v0;
          if (yocto::length_squared(edge) > yocto::length_squared(e1)) {
            e1 = edge;
          }
          if (k + 1 < poly.faces->v[f].size()) {
            vec3 edge2 = poly.vertices[poly.faces->v[f][k + 1]] - v0;
            vec3 n = yocto::cross(edge, edge2);
            if (yocto::length_squared(n) > yocto::length_squared(normal)) {
              normal = n;
            }
          }
        }
        vec3 n = yocto::normalize(normal);
        e1 = yocto::normalize(e1);
        vec3 e2 = yocto::cross(n, e1);
        vparams_.push_back({i, 1, p, e1, e2});
        bounds_.push_back({-delta, delta});
        bounds_.push_back({-delta, delta});

      } else if (incident_non_tri.size() == 2) {
        auto normal_fn = [&](int f) {
          vec3 v0 = poly.vertices[poly.faces->v[f][0]];
          vec3 normal = {0, 0, 0};
          for (size_t k = 1; k + 1 < poly.faces->v[f].size(); k++) {
            vec3 e0 = poly.vertices[poly.faces->v[f][k]] - v0;
            vec3 e1 = poly.vertices[poly.faces->v[f][k + 1]] - v0;
            vec3 n = yocto::cross(e0, e1);
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
          vparams_.push_back({i, 2, p, dir, vec3{0, 0, 0}});
          bounds_.push_back({-delta, delta});
        } else {
          vparams_.push_back({i, 3, p, vec3{0, 0, 0}, vec3{0, 0, 0}});
        }

      } else {
        // 3 or more non-triangular faces fix the vertex completely.
        vparams_.push_back({i, 3, p, vec3{0, 0, 0}, vec3{0, 0, 0}});
      }
    }
  }

  int NumParameters() const override {
    return (int)bounds_.size();
  }

  std::vector<std::pair<double, double>> Bounds() const override {
    return bounds_;
  }

  Polyhedron Apply(std::span<const double> params) const override {
    std::vector<vec3> pts = base_poly_.vertices;
    int arg_idx = 0;
    for (const VertexParam &vp : vparams_) {
      if (vp.num_constraints == 0) {
        pts[vp.v_idx] = vp.base_pos +
            vec3{params[arg_idx], params[arg_idx + 1], params[arg_idx + 2]};
        arg_idx += 3;
      } else if (vp.num_constraints == 1) {
        pts[vp.v_idx] = vp.base_pos +
            vp.dir1 * params[arg_idx] + vp.dir2 * params[arg_idx + 1];
        arg_idx += 2;
      } else if (vp.num_constraints == 2) {
        pts[vp.v_idx] = vp.base_pos + vp.dir1 * params[arg_idx];
        arg_idx += 1;
      }
    }

    Polyhedron new_poly;
    new_poly.vertices = std::move(pts);
    new_poly.faces = base_poly_.faces;
    new_poly.name = "improve";
    return new_poly;
  }
};

class PerturbNonTriangular : public Parameterization {
 private:
  struct PlaneParam {
    int face_idx;
    vec3 base_normal;
    vec3 base_center;
    vec3 base_ex, base_ey;
  };

  struct VertexParam {
    int v_idx;
    std::vector<int> incident_planes;
    vec3 base_pos;
  };

  Polyhedron base_poly_;
  std::vector<PlaneParam> plane_params_;
  std::vector<VertexParam> vparams_;
  std::vector<std::pair<double, double>> bounds_;

 public:
  void Prepare(const Polyhedron &poly, double delta) override {
    base_poly_ = poly;
    bounds_.clear();
    plane_params_.clear();
    vparams_.clear();

    double diam = Diameter(poly);
    double rot_delta = diam > 1e-9 ? delta / diam : 0.05;

    std::vector<int> face_to_plane(poly.faces->NumFaces(), -1);

    for (int f = 0; f < poly.faces->NumFaces(); ++f) {
      if (poly.faces->v[f].size() > 3) {
        face_to_plane[f] = plane_params_.size();

        vec3 v0 = poly.vertices[poly.faces->v[f][0]];
        vec3 normal = {0, 0, 0};
        vec3 e1 = {0, 0, 0};
        vec3 center = {0, 0, 0};
        for (int v_idx : poly.faces->v[f]) {
          center += poly.vertices[v_idx];
        }
        center /= (double)poly.faces->v[f].size();

        for (size_t k = 1; k < poly.faces->v[f].size(); k++) {
          vec3 edge = poly.vertices[poly.faces->v[f][k]] - v0;
          if (yocto::length_squared(edge) > yocto::length_squared(e1)) {
            e1 = edge;
          }
          if (k + 1 < poly.faces->v[f].size()) {
            vec3 edge2 = poly.vertices[poly.faces->v[f][k + 1]] - v0;
            vec3 n = yocto::cross(edge, edge2);
            if (yocto::length_squared(n) > yocto::length_squared(normal)) {
              normal = n;
            }
          }
        }
        vec3 n = yocto::normalize(normal);
        e1 = yocto::normalize(e1);
        vec3 e2 = yocto::cross(n, e1);

        plane_params_.push_back({f, n, center, e1, e2});

        bounds_.push_back({-rot_delta, rot_delta});
        bounds_.push_back({-rot_delta, rot_delta});
        bounds_.push_back({-delta, delta});
      }
    }

    for (int i = 0; i < poly.vertices.size(); ++i) {
      std::vector<int> incident_planes;
      for (int f = 0; f < poly.faces->NumFaces(); ++f) {
        if (face_to_plane[f] != -1) {
          for (int v : poly.faces->v[f]) {
            if (v == i) {
              incident_planes.push_back(face_to_plane[f]);
              break;
            }
          }
        }
      }

      vec3 p = poly.vertices[i];
      vparams_.push_back({i, incident_planes, p});

      if (incident_planes.empty()) {
        bounds_.push_back({-delta, delta});
        bounds_.push_back({-delta, delta});
        bounds_.push_back({-delta, delta});
      } else if (incident_planes.size() == 1) {
        bounds_.push_back({-delta, delta});
        bounds_.push_back({-delta, delta});
      } else if (incident_planes.size() == 2) {
        bounds_.push_back({-delta, delta});
      }
    }
  }

  int NumParameters() const override {
    return (int)bounds_.size();
  }

  std::vector<std::pair<double, double>> Bounds() const override {
    return bounds_;
  }

  Polyhedron Apply(std::span<const double> params) const override {
    int arg_idx = 0;

    struct PerturbedPlane {
      vec3 n;
      double d;
      vec3 ex, ey;
    };
    std::vector<PerturbedPlane> pplanes(plane_params_.size());

    for (size_t i = 0; i < plane_params_.size(); i++) {
      const auto& pp = plane_params_[i];
      double rot1 = params[arg_idx++];
      double rot2 = params[arg_idx++];
      double trans = params[arg_idx++];

      vec3 n = yocto::normalize(pp.base_normal + pp.base_ex * rot1 + pp.base_ey * rot2);
      vec3 ex = yocto::normalize(pp.base_ex - n * yocto::dot(pp.base_ex, n));
      vec3 ey = yocto::cross(n, ex);

      vec3 center = pp.base_center + n * trans;
      double d = yocto::dot(n, center);

      pplanes[i] = {n, d, ex, ey};
    }

    std::vector<vec3> pts = base_poly_.vertices;
    for (const VertexParam &vp : vparams_) {
      if (vp.incident_planes.empty()) {
        pts[vp.v_idx] = vp.base_pos +
            vec3{params[arg_idx], params[arg_idx + 1], params[arg_idx + 2]};
        arg_idx += 3;
      } else if (vp.incident_planes.size() == 1) {
        const auto& pl = pplanes[vp.incident_planes[0]];
        vec3 p = vp.base_pos;
        double dist = yocto::dot(p, pl.n) - pl.d;
        p -= pl.n * dist;
        p += pl.ex * params[arg_idx] + pl.ey * params[arg_idx + 1];
        pts[vp.v_idx] = p;
        arg_idx += 2;
      } else if (vp.incident_planes.size() == 2) {
        const auto& pl1 = pplanes[vp.incident_planes[0]];
        const auto& pl2 = pplanes[vp.incident_planes[1]];

        vec3 dir = yocto::cross(pl1.n, pl2.n);
        if (yocto::length_squared(dir) > 1e-12) {
          dir = yocto::normalize(dir);
          double n1n2 = yocto::dot(pl1.n, pl2.n);
          double det = 1.0 - n1n2 * n1n2;
          if (std::abs(det) > 1e-8) {
            double r1 = pl1.d - yocto::dot(pl1.n, vp.base_pos);
            double r2 = pl2.d - yocto::dot(pl2.n, vp.base_pos);
            double c1 = (r1 - r2 * n1n2) / det;
            double c2 = (r2 - r1 * n1n2) / det;
            vec3 p = vp.base_pos + pl1.n * c1 + pl2.n * c2;
            p += dir * params[arg_idx];
            pts[vp.v_idx] = p;
          } else {
            pts[vp.v_idx] = vp.base_pos;
          }
        } else {
          pts[vp.v_idx] = vp.base_pos;
        }
        arg_idx += 1;
      } else {
        const auto& pl1 = pplanes[vp.incident_planes[0]];
        const auto& pl2 = pplanes[vp.incident_planes[1]];
        const auto& pl3 = pplanes[vp.incident_planes[2]];

        vec3 n1 = pl1.n, n2 = pl2.n, n3 = pl3.n;
        double d1 = pl1.d, d2 = pl2.d, d3 = pl3.d;

        double det = yocto::dot(n1, yocto::cross(n2, n3));
        if (std::abs(det) > 1e-8) {
          vec3 p = (yocto::cross(n2, n3) * d1 +
                    yocto::cross(n3, n1) * d2 +
                    yocto::cross(n1, n2) * d3) / det;
          pts[vp.v_idx] = p;
        } else {
          pts[vp.v_idx] = vp.base_pos;
        }
      }
    }

    Polyhedron new_poly;
    new_poly.vertices = std::move(pts);
    new_poly.faces = base_poly_.faces;
    new_poly.name = "improve";
    return new_poly;
  }
};

static void Improve(int id) {
  DB db;
  const DB::Hard hard = db.GetHard(id);
  ArcFour global_rc(std::format("improve.{}.{}", id, time(nullptr)));

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

  StatusBar status(3);
  Periodically status_per(1.0);

  for (;;) {
    double input_shape_loss = EvaluateShapeLoss(aug);
    status.Print("Initial shape loss: {:.4f}\n", input_shape_loss);

    CHECK(CheckSameConnectivity(aug.poly, aug.poly.vertices)) <<
      std::format("Initial polyhedron fails CheckSameConnectivity! "
                  "Deg: {}, Cop: {}, Cvx: {}, Hsp: {}\n",
                  ctr_wrong_degenerate.Read(), ctr_wrong_coplanar.Read(),
                  ctr_wrong_convex.Read(), ctr_wrong_halfspace.Read());

    double diam = Diameter(aug.poly);
    double delta = 0.05 * diam;

    std::unique_ptr<Parameterization> param;
    if (true || global_rc.Word64() & 1) {
      param = std::make_unique<PerturbNonTriangular>();
    } else {
      param = std::make_unique<SlideNonTriangular>();
    }
    param->Prepare(aug.poly, delta);
    std::vector<std::pair<double, double>> bounds = param->Bounds();

    status.Print("Optimizing {} parameters across {} vertices.\n",
                 param->NumParameters(), aug.poly.vertices.size());

    std::unique_ptr<OptSeq> seq(new OptSeq(bounds, global_rc.Word64()));
    double best_shape_loss = input_shape_loss;

    Periodically flush_per(5 * 60);
    std::optional<Polyhedron> recent_best;
    Polyhedron best_poly = aug.poly;

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
    while (timer.Seconds() < 10 * 60) {
      status_per.RunIf([&]{
          std::string save_in = AGREY("nothing new");
          if (recent_best.has_value()) {
            save_in = std::format("save in {}",
                                  ANSI::Time(flush_per.SecondsLeft()));
          }

          status.Status(APURPLE("{}") " rounds. "
                        "{} attempts in {}, {} evaluated, {} bad\n"
                        "{} wrong ({} deg, {} cop, {} cvx, {} hsp)\n"
                        "best shape {} -> {}. {}",
                        ctr_rounds.Read(),
                        attempts,
                        ANSI::Time(timer.Seconds()),
                        ctr_evals.Read(),
                        ctr_final_poly_bad.Read(),
                        Util::FormatNum(ctr_wrong_connectivity.Read()),
                        Util::FormatNum(ctr_wrong_degenerate.Read()),
                        Util::FormatNum(ctr_wrong_coplanar.Read()),
                        Util::FormatNum(ctr_wrong_convex.Read()),
                        Util::FormatNum(ctr_wrong_halfspace.Read()),
                        input_shape_loss,
                        best_shape_loss,
                        save_in);
        });

      std::vector<double> arg = seq->Next();
      attempts++;

      // Produce the new vertex locations using the parameters.
      Polyhedron new_poly = param->Apply(arg);

      if (!IsWellConditioned(new_poly.vertices) ||
          !CheckSameConnectivity(aug.poly, new_poly.vertices)) {
        seq->Result(1e9);
        continue;
      }

      Aug new_aug(std::move(new_poly));
      double shape_loss = EvaluateShapeLoss(new_aug);

      double hard_loss = EvaluateHardnessLoss(hard.why, new_aug,
                                              shape_loss, input_shape_loss);

      const double loss = shape_loss + hard_loss;
      seq->Result(loss);

      // Only save to DB if it's still hard, and is a meaningful shape
      // improvement.
      if (hard_loss == 0.0 && shape_loss < best_shape_loss - 1e-4) {
        // Check the validity of the polyhedron before recording it too.
        const std::optional<Polyhedron> test_poly =
          PolyhedronFromConvexVertices(new_aug.poly.vertices);
        if (test_poly.has_value()) {
          best_shape_loss = shape_loss;
          status.Print("Improved shape loss to {:.4f}\n", shape_loss);
          best_poly = new_aug.poly;
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

    if (best_shape_loss < input_shape_loss) {
      status.Print("Re-evaluating hardness with 100x iterations...\n");
      Aug check_aug(best_poly);
      double check_loss =
        EvaluateHardnessLoss(hard.why, check_aug, 0.0, 1.0, 100);
      if (check_loss == 0.0) {
        status.Print("Hardness check passed. "
                     "Restarting with new best polyhedron.\n");
        aug = std::move(check_aug);
      } else {
        status.Print("Hardness check failed. "
                     "Restarting with previous polyhedron.\n");
      }
    } else {
      status.Print("No improvement found. "
                   "Restarting with same polyhedron.\n");
    }

    ctr_rounds++;
    status.Print("Destroy OptSeq...");
    seq.reset();
    status.Print("OK\n");
  }
}



int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 2) << "./improve.exe database-id\n";

  int id = atoi(argv[1]);
  CHECK(id > 0);

  Improve(id);

  return 0;
}
