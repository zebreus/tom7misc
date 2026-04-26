
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
#include "db.h"
#include "geom/mesh.h"
#include "geom/point-map.h"
#include "geom/polyhedra.h"
#include "geom/symmetry-groups.h"
#include "image.h"
#include "periodically.h"
#include "randutil.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "union-find.h"
#include "util.h"
#include "yocto-math.h"

using Aug = Albrecht::AugmentedPoly;

DECLARE_COUNTERS(ctr_poly, ctr_only_net, ctr_degenerate, ctr_too_big);

// (actually an upper bound, not inclusive)
static constexpr int MAX_FACES = 80;

static constexpr ColorUtil::Gradient BLACKBODY{
  GradRGB(0.0f, 0x333333),
  GradRGB(0.2f, 0x7700BB),
  GradRGB(0.5f, 0xFF0000),
  GradRGB(0.8f, 0xFFFF00),
  GradRGB(1.0f, 0xFFFFFF)
};

static TriangularMesh3D PolyToTriangularMesh(const Polyhedron &poly) {
  return TriangularMesh3D{.vertices = poly.vertices,
    .triangles = poly.faces->triangulation};
}

static void SaveAsSTL(const Polyhedron &poly, std::string_view filename) {
  TriangularMesh3D mesh = PolyToTriangularMesh(poly);
  OrientMesh(&mesh);
  return SaveAsSTL(mesh, filename, poly.name);
}

// Each of x,y,z in [-1, 1].
static vec3 RandomVec(ArcFour *rc) {
  return vec3(2.0 * RandDouble(rc) - 1.0,
              2.0 * RandDouble(rc) - 1.0,
              2.0 * RandDouble(rc) - 1.0);
}

struct Brechtfast {

  static constexpr int METHOD = DB::METHOD_RANDOM_SYMMETRIC;

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


  std::pair<int64_t, int64_t> Netness(const Aug &aug) {
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

          for (int sample = 0; sample < SAMPLES_PER_THREAD; sample++) {
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

    return std::make_pair(total, NUM_THREADS * SAMPLES_PER_THREAD);
  }

  double best_netness = 1.0;
  Brechtfast() : rc(std::format("hardiness.{}", time(nullptr))) {

  }

  ~Brechtfast() {

  }

  void Run() {
    DB db;

    LoadHisto();

    StatusBar status(1);
    Periodically status_per(1.0);
    Periodically histo_per(10.0);
    Periodically flush_per(59.0, false);
    Timer timer;

    // One channel for each face count, since we want the whole pareto
    // frontier.
    std::vector<std::optional<std::tuple<Polyhedron, int, int64_t, int64_t>>>
      new_best(MAX_FACES, std::nullopt);

    for (;;) {

      const int num_verts = 8 + RandTo(&rc, 54);

      Aug aug = [&]() {
          switch (METHOD) {
          case DB::METHOD_RANDOM_CYCLIC:
            return Aug(RandomCyclicPolyhedron(&rc, num_verts));
          case DB::METHOD_RANDOM_SYMMETRIC:
            return Aug(RandomSymmetricPolyhedron(&rc, num_verts));
          default:
            LOG(FATAL) << "Bad method?";
          }
        }();

      const Polyhedron &poly = aug.poly;
      const int nfaces = poly.faces->NumFaces();
      const int nedges = poly.faces->NumEdges();
      const int nverts = poly.faces->NumVertices();

      const auto &[numer, denom] = Netness(aug);
      const double netness = numer / (double)denom;
      ctr_poly++;
      if (numer == denom) {
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
              numer, denom);

        }();
      }

      if (netness < best_netness) {
        std::string filename =
          std::format("brecht-{}-{:.5g}.stl", time(nullptr),
                      netness * 100.0);
        status.Print(AGREEN("New best!") " {} faces, {} edges, {} vert. "
                     "Wrote {}\n", nfaces, nedges, nverts, filename);
        SaveAsSTL(poly, filename);
        // best_netness = netness;
      }

      status_per.RunIf([&]{
          status.Status("{} polys, {} only, best {:.7g}, {}\n",
                        ctr_poly.Read(),
                        ctr_only_net.Read(),
                        best_netness,
                        ANSI::Time(timer.Seconds()));
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
