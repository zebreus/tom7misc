
#include "closure-conversion.h"

#include <string>

#include "il.h"
#include "il-pass.h"

// Closure conversion makes all functions in the program
// into globals. Globals are closed except for other globals.

namespace il {

struct ConvertPass : public Pass<> {
  using Pass::Pass;

  const Exp *DoFn(const std::string &self,
                  const std::string &x,
                  const Exp *body,
                  const Exp *guess) {


    return pool->Fn(self, x, DoExp(body, args...), guess);
  }


};

Program ClosureConversion::Convert(const Program &pgm) {

}


}  // namespace il
