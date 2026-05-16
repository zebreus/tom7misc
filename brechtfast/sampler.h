
#ifndef _BRECHTFAST_SAMPLER_H
#define _BRECHTFAST_SAMPLER_H

#include <cstdint>
#include <string>

#include "albrecht.h"
#include "arcfour.h"
#include "geom/polyhedra.h"
#include "status-bar.h"

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

  static Polyhedron MakeConstruct(StatusBar *status,
                                  ArcFour *rc,
                                  int max_faces);

  static OneSample ConstructSample(StatusBar *status,
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
