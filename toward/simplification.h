
#ifndef _TOWARD_SIMPLIFICATION_H
#define _TOWARD_SIMPLIFICATION_H

#include <memory>

#include "minitable.h"
#include "prop.h"

struct Simplification {

  Simplification();

  Prop Simplify(const Prop &in) const;

 private:
  std::unique_ptr<MiniTable> table;
};

#endif
