
#include "albrecht.h"

#include <ctime>
#include <format>
#include <string_view>
#include <utility>
#include <vector>
#include <span>
#include <cstdint>
#include <cstdio>

#include "ansi.h"
#include "arcfour.h"
#include "base/print.h"
#include "bit-string.h"
#include "geom/polyhedra.h"
#include "randutil.h"
#include "threadutil.h"
#include "timer.h"
#include "union-find.h"

struct Hardiness {

  static constexpr int SAMPLES_PER_THREAD = 16384 * 8;
  static constexpr int NUM_THREADS = 8;

  ArcFour rc;

  double Netness(Polyhedron poly_in) {
    Albrecht::AugmentedPoly aug(std::move(poly_in));

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

    return total / (double)(NUM_THREADS * SAMPLES_PER_THREAD);
  }

  FILE *file = nullptr;

  Hardiness() : rc(std::format("hardiness.{}", time(nullptr))) {
    file = fopen("hardiness.txt", "wb");
    CHECK(file != nullptr);
  }

  ~Hardiness() { fclose(file); }

  void WriteStats(Polyhedron poly, std::string_view name) {
    const int nfaces = poly.faces->NumFaces();
    const int nedges = poly.faces->NumEdges();
    const int nverts = poly.faces->NumVertices();

    Timer timer;
    double netness = Netness(std::move(poly));
    Print(file, "{}\t{}\t{}\t{}\t{:.11g}\n",
          nverts, nedges, nfaces,
          name, netness);
    fflush(file);
    Print(AWHITE("{}") " has netness " ABLUE("{:.7g}%") " in {}\n",
          name, 100.0 * netness, ANSI::Time(timer.Seconds()));
  }

  void Run() {
    WriteStats(Icosahedron(), "icos");
    WriteStats(Dodecahedron(), "dodec");
    WriteStats(Noperthedron(), "nope");
    WriteStats(Onperthedron(), "onpe");

    WriteStats(TruncatedTetrahedron(), "truncatedtetrahedron");
    WriteStats(Cuboctahedron(), "cuboctahedron");
    WriteStats(TruncatedCube(), "truncatedcube");
    WriteStats(TruncatedOctahedron(), "truncatedoctahedron");
    WriteStats(Rhombicuboctahedron(), "rhombicuboctahedron");
    WriteStats(TruncatedCuboctahedron(), "truncatedcuboctahedron");
    WriteStats(SnubCube(), "snubcube");
    WriteStats(Icosidodecahedron(), "icosidodecahedron");
    WriteStats(TruncatedDodecahedron(), "truncateddodecahedron");
    WriteStats(TruncatedIcosahedron(), "truncatedicosahedron");
    WriteStats(Rhombicosidodecahedron(), "rhombicosidodecahedron");
    WriteStats(TruncatedIcosidodecahedron(), "truncatedicosidodecahedron");
    WriteStats(SnubDodecahedron(), "snubdodecahedron");

    WriteStats(TriakisTetrahedron(), "triakistetrahedron");
    WriteStats(RhombicDodecahedron(), "rhombicdodecahedron");
    WriteStats(TriakisOctahedron(), "triakisoctahedron");
    WriteStats(TetrakisHexahedron(), "tetrakishexahedron");
    WriteStats(DeltoidalIcositetrahedron(), "deltoidalicositetrahedron");
    WriteStats(DisdyakisDodecahedron(), "disdyakisdodecahedron");
    WriteStats(DeltoidalHexecontahedron(), "deltoidalhexecontahedron");
    WriteStats(PentagonalIcositetrahedron(), "pentagonalicositetrahedron");
    WriteStats(RhombicTriacontahedron(), "rhombictriacontahedron");
    WriteStats(TriakisIcosahedron(), "triakisicosahedron");
    WriteStats(PentakisDodecahedron(), "pentakisdodecahedron");
    WriteStats(DisdyakisTriacontahedron(), "disdyakistriacontahedron");
    WriteStats(PentagonalHexecontahedron(), "pentagonalhexecontahedron");
  }
};

int main(int argc, char **argv) {
  ANSI::Init();

  {
    Hardiness hardiness;
    hardiness.Run();
  }

  return 0;
}
