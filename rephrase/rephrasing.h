
#ifndef _REPHRASE_REPHRASING_H
#define _REPHRASE_REPHRASING_H

#include "document.h"

struct Rephrasing {
  Rephrasing();

  // Return the next best rephrasing and its loss.
  // The first call returns the input with no loss.
  std::pair<DocTree, double> NextRephrasing(const DocTree &in);
};

#endif
