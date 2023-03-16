
#ifndef _GRAD_TESTU01_H
#define _GRAD_TESTU01_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usoft.h"
#include "unif01.h"

#ifdef __cplusplus
}
#endif

#include <functional>
#include <string>

struct Generator {
  // Populate a TestU01 gen instance which gets exclusive
  // access to this state.
  virtual void FillGen(unif01_Gen *gen) = 0;
  virtual ~Generator() {}
};

struct BigCrushTestResult {
  std::string name;
  double p_value;
};

std::vector<BigCrushTestResult> ParallelBigCrush(
    const std::function<Generator*()> &gengen,
    const std::string &filepart,
    int NUM_THREADS = 12);

#endif
