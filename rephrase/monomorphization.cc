
#include "monomorphization.h"

#include <cstdint>

#include "il.h"

namespace il {

Monomorphization::Monomorphization(AstPool *pool) :
  pool(pool) {}

void Monomorphization::SetVerbose(int v) {
  verbose = v;
}

// Monomorphization eliminates type abstractions (and thus type variables)
// from the program. This makes the type theory much simpler, and enables
// a number of optimizations, but it has the downside of increasing the
// program size, perhaps exponentially! This is because the main technique
// for monomorphizing a polymorphic value is to duplicate it.
//
// A global may have some tyvars and some uses within the program;
// each use instantiates the tyvars. It can be made monomorphic by
// creating new monotyped copies, each one instantiated at a different
// set of type arguments. We can only do this when the type args are
// closed, though. Since two globals can reference one another
// recursively, this means that we cannot simply do this process one
// global at a time.
//
//   global (a) f (x : a) = g<a> x
//   global (b) g (y : b) = f<b> y
//
// There are several other constructs that bind type variables:
//
//   let (a) x = rhs
//   in e
//   end
//
// Because of the value restriction, the rhs is a value* here. That
// means that we can replicate the binding, once for each set of
// type args that x is used at within the body.
//
//  * I think that there are some parts of elaboration that violate
//    the value restriction, so we need to check on those.
//
// pack and unpack. These are introduced by closure conversion.
//
//   pack t as ∃α.t' of e
//
//   unpack α,x = rhs
//   in body
//
// The reason for packing is to make all functions with the same EL
// type have the same IL type after closure conversion (despite having
// potentially different environment types). This is necessary since a
// higher-order function (even a monomorphic one) can be called with
// different function arguments dynamically. One way to deal with this
// is to aggregate all possible environment types into a single
// datatype "env" and use that, so any function t1 -> t2 is
// represented uniformly as t1 * env -> t2. The annoying thing about
// this is that the body of the function then needs to unpack the
// environment, doing a dynamic check that it has the expected type.
//
// Another way you could do it is to represent the function itself
// as a giant datatype "func," so that the arm represents not only
// the environment but the code that accepts it, like
//
//   datatype func =
//       F1 of {x : int} * ({ x : int } * string -> bool)
//     | F2 of {y : bool, z : string } *
//               ({ y : bool, z : string } * string -> bool)
//     | ...
//
// here either F1(env1, g1) or F2(env2, g2) could be the representation
// of a function from string -> bool. To use such a function, we do a
// case analysis
//   case f of
//       F1 (env, f) => f (env, arg)
//     | F2 (env, f) => f (env, arg)
//     ...
//
// The advantage of this is that whenever a function argument is known,
// we can inline and reduce the case to the appropriate arm. A little
// better would be to have the translation itself keep the mapping from
// the constructors Fn to the corresponding global. Then we have
//
//   datatype func =
//       F1 of {x : int}
//     | F2 of {y : bool, z : string }
//
// but we do not deduplicate by type as in the first one, as the ctor
// is what tells us the correct global to dispatch to.
//
//   case f of
//       F1 env => g1 (env, arg)
//     | F2 env => g2 (env, arg)
//     ...
//
// This is better because the global will always be a constant (so
// there is just one dynamic dispatch even when using higher-order
// functions). Note that we cannot even emit arms that have the wrong
// type (for the argument or return) here, as they would be ill-typed.
// So this really suggests that we have a different datatype "func"
// for each function type. We could also use some escape analysis to
// constrain this to just the set of functions that could reach a
// particular point.
//
// With this approach to closure conversion, we are no longer using
// pack/unpack, so we don't need to worry about those for
// monomorphization. But we can only do this transformation after
// monomorphization, because it requires globally deduplicating by type!
// So basically this leads to monomorphization happening before
// (this different approach to) closure conversion.
//
// This leaves the IL TypeFn construct. This is used in classic closure
// conversion and for a similar purpose when elaborating mutually
// recursive functions. So if we're able to bypass those, we really just
// have to worry about let and defunctionalization.

Program Monomorphization::Monomorphize(const Program &) {

}

}  // il

#endif
