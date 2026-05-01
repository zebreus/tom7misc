
#include "albrecht.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
#include <span>
#include <cstdint>
#include <cstdio>

#include "ansi-image.h"
#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "bit-string.h"
#include "color-util.h"
#include "construct.h"
#include "db.h"
#include "geom/point-map.h"
#include "geom/polyhedra.h"
#include "geom/symmetry-groups.h"
#include "image.h"
#include "opt/opt.h"
#include "periodically.h"
#include "poly-util.h"
#include "randutil.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "union-find.h"
#include "util.h"
#include "yocto-math.h"

using Aug = Albrecht::AugmentedPoly;

DECLARE_COUNTERS(ctr_poly, ctr_only_net, ctr_degenerate, ctr_too_big,
                 ctr_ill_conditioned, ctr_not_manifold, ctr_no_angle,
                 ctr_no_feasible);

DECLARE_COUNTERS(ctr_face_not_feasible);

// (actually an upper bound, not inclusive)
static constexpr int MAX_FACES = 80;

static constexpr ColorUtil::Gradient BLACKBODY{
  GradRGB(0.0f, 0x333333),
  GradRGB(0.2f, 0x7700BB),
  GradRGB(0.5f, 0xFF0000),
  GradRGB(0.8f, 0xFFFF00),
  GradRGB(1.0f, 0xFFFFFF)
};

// Each of x,y,z in [-1, 1].
static vec3 RandomVec(ArcFour *rc) {
  return vec3(2.0 * RandDouble(rc) - 1.0,
              2.0 * RandDouble(rc) - 1.0,
              2.0 * RandDouble(rc) - 1.0);
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

struct Brechtfast {

  static constexpr int METHOD =
    DB::METHOD_CONSTRUCT;
    // DB::METHOD_OPT;
    // DB::METHOD_RANDOM_SYMMETRIC;

  static constexpr int SAMPLES_PER_THREAD = 16384;
  static constexpr int NUM_THREADS = 8;

  ArcFour rc;

  std::vector<int> histo = std::vector<int>(MAX_FACES * 101, 0);
  int &HistoCell(int faces, int pct) {
    return histo[faces * 101 + pct];
  }

  std::string HistoFile() {
    return std::format("brechtfast-{}-{}.histo", METHOD, MAX_FACES);
  }

  void SaveHisto() {
    std::string out;
    for (int i = 0; i < histo.size(); i++) {
      AppendFormat(&out, "{}\n", histo[i]);
    }
    Util::WriteFile(HistoFile(), out);
  }

  void LoadHisto() {
    std::vector<std::string> vals = Util::ReadFileToLines(HistoFile());
    if (vals.size() != MAX_FACES * 101) {
      Print("Invalid or missing histo file.\n");
      return;
    }

    for (int i = 0; i < vals.size(); i++) {
      histo[i] = Util::ParseInt64(vals[i], 0);
    }
  }

  // All of the vertices on the unit sphere.
  static Polyhedron RandomCyclicPolyhedron(ArcFour *rc, int num_points) {
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

  // from noperts. See discussion there.
  static Polyhedron RandomSymmetricPolyhedron(ArcFour *rc, int num_points) {
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
        if (poly.value().faces->NumFaces() >= MAX_FACES) {
          ctr_too_big++;
          continue;
        }
        return std::move(poly.value());
      } else {
        ctr_degenerate++;
      }
    }
  }


  std::pair<int64_t, int64_t> Netness(const Aug &aug,
                                      int samples_per_thread =
                                      SAMPLES_PER_THREAD) {
    const Faces &faces = *aug.poly.faces;
    const int num_faces = faces.NumFaces();
    const int num_edges = faces.NumEdges();

    uint64_t seed = Rand64(&rc);
    std::vector<int> nets(NUM_THREADS, 0);
    ParallelFan(
        NUM_THREADS,
        [&](int thread_idx) {
          ArcFour thread_rc(std::format("{}.{}", seed, thread_idx));

          int success = 0;

          std::vector<int> edges;
          for (int i = 0; i < num_edges; i++)
            edges.push_back(i);

          for (int sample = 0; sample < samples_per_thread; sample++) {
            BitString unfolding(num_edges, false);
            // Greedily connect them, but in some random order.
            Shuffle(&thread_rc, &edges);

            UnionFind uf(num_faces);
            for (int eidx : edges) {
              const Faces::Edge &edge = faces.edges[eidx];
              if (uf.Find(edge.f0) != uf.Find(edge.f1)) {
                uf.Union(edge.f0, edge.f1);
                unfolding.Set(eidx, true);
              }
            }

            if (Albrecht::IsNet(aug, unfolding)) {
              success++;
            }
          }

          nets[thread_idx] = success;
        });

    int total = 0;
    for (int s : nets) total += s;

    return std::make_pair(total, NUM_THREADS * samples_per_thread);
  }

  static constexpr int SAMPLE_LINE = 0;
  StatusBar status = StatusBar(3);

  double time_sample = 0.0;
  double time_measure = 0.0;

  double best_netness = 1.0;
  Brechtfast() : rc(std::format("hardiness.{}", time(nullptr))) {

  }

  ~Brechtfast() {

  }

  struct OneSample {
    Aug aug;
    int64_t numer = 0, denom = 0;
  };

  OneSample ConstructSample() {
    Timer sample_timer;
    for (;;) {
      const int target_faces = 12 + RandTo(&rc, 29);
      PartialPolyhedron pp(&rc, target_faces, 100);

      while (pp.NumFaces() < target_faces) {
        std::vector<int> b_edges = pp.GetBoundaryEdges();
        if (b_edges.empty()) {
          break;
        }
        Shuffle(&rc, &b_edges);

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

          double angle = min_angle + RandDouble(&rc) * subtended;
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

          double best_overlap = -1.0;
          std::vector<vec2> best_poly;

          for (int sample = 0; sample < 100; ++sample) {
            double u = RandDouble(&rc);
            double v = RandDouble(&rc);
            std::vector<vec2> poly = chooser.Generate2DFace(u, v);

            double overlap = pp.MeasureOverlapFraction(edge_idx, poly);
            if (overlap > best_overlap) {
              best_overlap = overlap;
              best_poly = std::move(poly);
            }
          }

          if (best_overlap >= 0.0) {
            std::vector<vec3> best_face = chooser.ConvertTo3D(best_poly);
            if (!pp.IsFeasible(edge_idx, best_face)) {
              status.Print("Bug... Not Feasible!\n");
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
      if (opoly.value().faces->NumFaces() >= MAX_FACES) {
        ctr_too_big++;
        continue;
      }

      time_sample += sample_timer.Seconds();

      Timer measure_timer;
      Aug aug(std::move(opoly.value()));
      auto [numer, denom] = Netness(aug);
      OneSample sample{.aug = std::move(aug), .numer = numer, .denom = denom};
      time_measure += measure_timer.Seconds();

      return sample;
    }
  }

  OneSample OptSample() {
    // Naive black-box optimization.

    const int num_verts = 12 + RandTo(&rc, 20);

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
        auto [numer, denom] = Netness(aug, 128);

        calls++;
        if ((calls % 128) == 0) {
          status_per.RunIf([&]{
              status.LineStatus(
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
                    1, 1, Rand64(&rc));

    std::optional<Polyhedron> opoly = MakePoly(best);
    CHECK(opoly.has_value());
    Aug aug(std::move(opoly.value()));
    auto [numer, denom] = Netness(aug);
    return OneSample{.aug = std::move(aug), .numer = numer, .denom = denom};
  }

  OneSample Sample(int method) {
    switch (METHOD) {
    case DB::METHOD_RANDOM_CYCLIC: {
      const int num_verts = 8 + RandTo(&rc, 54);
      Aug aug = Aug(RandomCyclicPolyhedron(&rc, num_verts));
      const auto &[numer, denom] = Netness(aug);
      return OneSample{.aug = std::move(aug), .numer = numer, .denom = denom};
    }

    case DB::METHOD_RANDOM_SYMMETRIC: {
      const int num_verts = 8 + RandTo(&rc, 54);
      Aug aug = Aug(RandomSymmetricPolyhedron(&rc, num_verts));
      const auto &[numer, denom] = Netness(aug);
      return OneSample{.aug = std::move(aug), .numer = numer, .denom = denom};
    }

    case DB::METHOD_OPT: {
      return OptSample();
    }

    case DB::METHOD_CONSTRUCT: {
      return ConstructSample();
    }

    default:
      LOG(FATAL) << "Bad method?";
    }


  }

  void Run() {
    DB db;

    LoadHisto();

    Periodically status_per(1.0);
    Periodically histo_per(10.0);
    Periodically flush_per(59.0, false);
    Timer timer;

    // One channel for each face count, since we want the whole pareto
    // frontier.
    std::vector<std::optional<std::tuple<Polyhedron, int, int64_t, int64_t>>>
      new_best(MAX_FACES, std::nullopt);

    for (;;) {

      OneSample sample = Sample(METHOD);

      const Polyhedron &poly = sample.aug.poly;
      const int nfaces = poly.faces->NumFaces();
      const int nedges = poly.faces->NumEdges();
      const int nverts = poly.faces->NumVertices();

      double netness = sample.numer / (double)sample.denom;
      ctr_poly++;
      if (sample.numer == sample.denom) {
        ctr_only_net++;
      }

      {
        int pct = std::clamp((int)std::round(100.0 * netness), 0, 100);
        HistoCell(nfaces, pct)++;

        [&]{
          for (int p = pct - 1; p >= 0; p--) {
            if (HistoCell(nfaces, p) > 0) {
              return;
            }
          }

          // Then it must be the best in this channel.
          new_best[nfaces] = std::make_tuple(
              poly, METHOD,
              sample.numer, sample.denom);

        }();
      }

      if (netness < best_netness) {
        std::string filename =
          std::format("brecht-{}-{:.5g}.stl", time(nullptr),
                      netness * 100.0);
        status.Print(AGREEN("New best!") " {} faces, {} edges, {} vert. "
                     "Wrote {}\n", nfaces, nedges, nverts, filename);
        SaveAsSTL(poly, filename);
        best_netness = netness;
      }

      status_per.RunIf([&]{
          double total_time = timer.Seconds();
          double sample_pct = (time_sample * 100.0) / total_time;
          double measure_pct = (time_measure * 100.0) / total_time;

          status.Status(
              // First line reserved for subprocess
              "\n"
              "{} ill, {} notman, {} degen, {} noθ, {} no∆, {}✘\n"
              "{} polys, {} only, best {:.7g}, {} ({:1f}% + {:1f}%) \n",
              ctr_ill_conditioned.Read(),
              ctr_not_manifold.Read(),
              ctr_degenerate.Read(),
              ctr_no_angle.Read(),
              ctr_no_feasible.Read(),
              ctr_face_not_feasible.Read(),
              ctr_poly.Read(),
              ctr_only_net.Read(),
              best_netness,
              ANSI::Time(total_time),
              sample_pct, measure_pct);
        });

      histo_per.RunIf([&] {
        int norm = 0;
        for (int n : histo)
          norm = std::max(norm, n);
        if (norm > 0) {
          ImageRGBA img(MAX_FACES, 101);
          for (int y = 0; y <= 100; y++) {
            for (int x = 0; x < MAX_FACES; x++) {
              int count = HistoCell(x, 100 - y);
              double f = count / (double)norm;
              uint32_t c = count == 0
                               ? 0x000000FF
                               : ColorUtil::LinearGradient32(BLACKBODY, f);
              img.SetPixel32(x, y, c);
            }
          }

          // Crop out left column because there are no such
          // polyhedra, and we want to fit in 80 columns.
          img = img.Crop32(3, 0, img.Width() - 3, img.Height());

          status.Print("{}\n", ANSIImage::HalfChar(img));
        }
      });

      flush_per.RunIf([&]{
          SaveHisto();
          bool any = false;
          for (auto &ov : new_best) {
            if (ov.has_value()) {
              any = true;
              const auto &[poly, method, numer, denom] = ov.value();
              db.AddHard(poly, method, numer, denom);
            }
            ov = std::nullopt;
          }
          if (any) {
            status.Print("Saved to DB.");
          }
        });
    }
  }

};

int main(int argc, char **argv) {
  ANSI::Init();

  {
    Brechtfast brechtfast;
    brechtfast.Run();
  }

  return 0;
}
