
#ifndef _TOWARD_LAYOUT_REDUCER_H
#define _TOWARD_LAYOUT_REDUCER_H

#include <functional>
#include <memory>

#include "cell-library.h"
#include "layout.h"

struct Reducer {
  virtual Layout ReduceWhile(
      Layout layout, int max_consecutive_failures,
      std::function<bool(const Layout &layout)> pred) = 0;

  static std::unique_ptr<Reducer> Create(const CellLibrary &library);
  virtual ~Reducer();

 protected:
  Reducer();
};

#endif
