
#ifndef _PORTMANTOUT_MAKEPARTICLES_H
#define _PORTMANTOUT_MAKEPARTICLES_H

#include <vector>
#include <string>

struct ArcFour;

struct Round {
  std::vector<std::string> path;
  std::vector<std::string> covered;
};

struct Trace {
  std::vector<Round> rounds;
};

// Trace can be null; it's expensive.
std::vector<std::string> MakeParticles(
    ArcFour *rc, const std::vector<std::string> &dict, bool verbose,
    Trace *trace);

#endif
