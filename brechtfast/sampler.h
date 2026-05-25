
#ifndef _BRECHTFAST_SAMPLER_H
#define _BRECHTFAST_SAMPLER_H

#include <cstdint>
#include <string>

#include "albrecht.h"
#include "arcfour.h"
#include "geom/polyhedra.h"
#include "status-bar.h"

struct PartialPolyhedron;

struct Sampler {
  using Aug = Albrecht::AugmentedPoly;

  // We assume we can write to this line of the status bar
  // while sampling.
  static constexpr int SAMPLE_LINE = 0;

  struct OneSample {
    Aug aug;
    int64_t numer = 0, denom = 0;
    double sample_sec = 0.0, measure_sec = 0.0;
  };

  // If leaf_ih is set, then we add a constraint to some edge/leaf
  // for the pool of valid unfoldings. This makes the adversarial
  // polyhedra less likely to have a leaf net for that choice, but
  // probably more likely to have a net overall.
  static Polyhedron MakeConstruct(StatusBar *status,
                                  ArcFour *rc,
                                  int max_faces,
                                  bool leaf_ih);

  static OneSample ConstructSample(StatusBar *status,
                                   ArcFour *rc,
                                   int max_faces);

  struct LeafIHSample {
    // A polyhedron specifically sampled to be hard under the
    // constraint that the indicated face/edge is a leaf in
    // the net.
    Polyhedron poly;
    int face_idx = 0;
    int edge_idx = 0;
  };
  static LeafIHSample ConstructHardLeaf(StatusBar *status,
                                        ArcFour *rc,
                                        int max_faces);

  static OneSample Sample(uint64_t seed, const Polyhedron &poly);

  // Sample using black-box optimizer.
  static OneSample OptSample(StatusBar *status,
                             ArcFour *rc);

  static Polyhedron RandomCyclicPolyhedron(ArcFour *rc, int num_points);
  static Polyhedron RandomSymmetricPolyhedron(ArcFour *rc, int num_points,
                                              int max_faces);

  static std::string SampleStats();
};


#endif
