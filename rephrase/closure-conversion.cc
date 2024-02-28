
#include "closure-conversion.h"

#include <string>

#include "il.h"
#include "il-typed-pass.h"

// Closure conversion makes all functions in the program
// into globals. Globals are closed except for other globals.

namespace il {

struct ConvertPass : public TypedPass<> {
  using TypedPass::TypedPass;

  virtual std::pair<const Exp *, const Type *>
  DoFn(Context G,
       const std::string &self,
       const std::string &x,
       const Type *arrow_type,
       const Exp *body,
       const Exp *guess) {

    // We do a similar thing to compile mutually-recursive fun
    // declarations (see elaboration.cc) but it's easier here, because
    // among other things, there is just one function.
    //
    // How to deal with polymorphism though? I guess we can abstract
    // over any type variables in the context, like we'll be abstracting
    // over the expression variables to form the environment.
    //
    // What about free variables with polymorphic type?

    // The type a -> b
    // becomes
    //  ∃α. {1: α, 2: {1: α, 2:a} -> b}
    // α is the environment type. The function is represented as
    // a pair of the environment and the closed function that now
    // takes an additional environment argument.
    //
    // and (e1 e2)
    // is therefore
    // unpack α,f = e1
    // in (#2 f){1 = #1 f, 2 = e2}
    //
    // That is, we unpack the environment and call the function
    // with it and the original argument.
    //
    // A function expression (fn as self x => body) is
    // translated as follows. Let x1:t1, ... xn:tn be the free
    // variables in "body". Then the function expression is
    // pack {1:t1, 2:t2, ...} as α.{1: α, 2: {1: α, 2:a} -> b} of g
    //
    // where g is a new global symbol
    //
    // global g = fn as self {1 = env, 2 = x} =>
    //      let x1 = #1 env
    //      let x2 = #2 env
    //             ...
    //      in body end

    const Type *at = DoType(G, arrow_type, args...);
    const auto &[dom, cod] = arrow_type->Arrow();
    Context GG = G.Insert(self, {{}, at}).Insert(x, {{}, dom});
    const auto &[be, bt] = DoExp(GG, body, args...);
    return {pool->Fn(self, x, at, be, guess), at};
  }

  std::vector<Global> globals_added;

};

Program ClosureConversion::Convert(const Program &pgm) {

}


}  // namespace il
