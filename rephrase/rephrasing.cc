#include "rephrasing.h"

#include "document.h"

// XXX model, cached database, etc.
Rephrasing::Rephrasing() {}

std::pair<DocTree, double> Rephrasing::NextRephrasing(const DocTree &in) {
  return std::make_pair(in, 0.0);
}
