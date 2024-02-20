#ifndef _REPHRASE_UNCURRY_H
#define _REPHRASE_UNCURRY_H

#include "el.h"

namespace el {

struct Uncurry {
  explicit Uncurry(AstPool *pool);

  // Rewrites
  //   fun f p1 p2 p3 = e1
  //     | f q1 q2 q3 = e2
  //     ...
  // to
  //   fun f v1 =
  //      fn v2 =>
  //      fn v3 =>
  //        case (v1, v2, v3) of
  //             (p1, p2, p3) => e1
  //           | (p1, p2, p3) => e2
  //
  // This is a purely syntactic rewrite which doesn't change the
  // type of f nor the variables bound in e1 or e2 (except for the
  // fresh v1,v2,v3).
  //
  // Doesn't do anything if the function isn't curried.

  const Exp *Rewrite(const Exp *);

 private:
  AstPool *pool;
};

}  // el

#endif
