
#ifndef _BRECHTFAST_NETNESS_H
#define _BRECHTFAST_NETNESS_H

#include <cstdint>
#include <optional>
#include <utility>

#include "albrecht.h"
#include "bit-string.h"

struct Netness {
  using Aug = Albrecht::AugmentedPoly;

  // Stochastically compute the "netness" of the polyhedron, which is
  // the fraction of random unfoldings that are valid nets. This is
  // given as a discrete fraction with a numerator of at least
  // num_samples.
  static std::pair<int64_t, int64_t> Compute(uint64_t seed,
                                             const Aug &aug,
                                             int num_samples,
                                             int num_repeat = 8,
                                             int num_threads = 8);
  struct NetnessResult {
    int64_t numer = 0;
    int64_t denom = 0;
    // An example net, unless numer is zero.
    std::optional<BitString> example;
  };

  static NetnessResult ComputeWithExample(uint64_t seed,
                                          const Aug &aug,
                                          int num_samples,
                                          int num_repeat = 8,
                                          int num_threads = 8);

};

#endif
