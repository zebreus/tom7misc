
#include "albrecht.h"

#include <ctime>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
#include <span>
#include <cstdint>
#include <cstdio>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "base/print.h"
#include "bit-string.h"
#include "db.h"
#include "geom/mesh.h"
#include "geom/polyhedra.h"
#include "periodically.h"
#include "randutil.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "union-find.h"
#include "yocto-math.h"

using Aug = Albrecht::AugmentedPoly;

DECLARE_COUNTERS(ctr_poly, ctr_only_net, ctr_degenerate);

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

  static constexpr int SAMPLES_PER_THREAD = 16384;
  static constexpr int NUM_THREADS = 8;

  ArcFour rc;


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

    StatusBar status(1);
    Periodically status_per(1.0);
    Periodically flush_per(59.0, false);
    Timer timer;

    std::optional<std::tuple<Polyhedron, int, int64_t, int64_t>> new_best;
    for (;;) {

      const int num_verts = 8 + RandTo(&rc, 54);
      Aug aug = Aug(RandomCyclicPolyhedron(&rc, num_verts));
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

      if (netness < best_netness) {
        std::string filename =
          std::format("brecht-{}-{:.5g}.stl", time(nullptr),
                      netness * 100.0);
        status.Print(AGREEN("New best!") " {} faces, {} edges, {} vert. "
                     "Wrote {}\n", nfaces, nedges, nverts, filename);
        SaveAsSTL(poly, filename);
        best_netness = netness;
        new_best = std::make_tuple(
            poly, DB::METHOD_RANDOM_CYCLIC,
            numer, denom);
      }

      status_per.RunIf([&]{
          status.Status("{} polys, {} only, best {:.7g}, {}\n",
                        ctr_poly.Read(),
                        ctr_only_net.Read(),
                        best_netness,
                        ANSI::Time(timer.Seconds()));
        });

      flush_per.RunIf([&]{
          if (new_best.has_value()) {
            const auto &[poly, method, numer, denom] = new_best.value();
            db.AddHard(poly, method, numer, denom);
            new_best = std::nullopt;
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
